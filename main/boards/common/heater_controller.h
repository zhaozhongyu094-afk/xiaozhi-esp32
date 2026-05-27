#ifndef __HEATER_CONTROLLER_H__
#define __HEATER_CONTROLLER_H__

#include "mcp_server.h"
#include "ntc_temperature_sensor.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cmath>

class HeaterController {
private:
    static constexpr const char* TAG = "HeaterController";
    static constexpr uint32_t kMaxDuty = 8191;
    static constexpr int64_t kPidPeriodUs = 1000000;

    gpio_num_t gpio_num_;
    ledc_timer_t timer_num_;
    ledc_channel_t channel_;
    uint32_t frequency_hz_;
    NtcTemperatureSensor& temperature_sensor_;
    int max_target_temperature_c_;
    float kp_;
    float ki_;
    float kd_;
    esp_timer_handle_t pid_timer_ = nullptr;

    int target_temperature_c_ = 0;
    float current_temperature_c_ = NAN;
    float pid_integral_ = 0.0f;
    float previous_error_ = 0.0f;
    int output_power_percent_ = 0;
    bool enabled_ = false;

    static int ClampInt(int value, int min_value, int max_value) {
        if (value < min_value) {
            return min_value;
        }
        if (value > max_value) {
            return max_value;
        }
        return value;
    }

    static float ClampFloat(float value, float min_value, float max_value) {
        if (value < min_value) {
            return min_value;
        }
        if (value > max_value) {
            return max_value;
        }
        return value;
    }

    uint32_t PowerToDuty(int power_percent) const {
        if (power_percent <= 0) {
            return 0;
        }
        if (power_percent >= 100) {
            return kMaxDuty;
        }
        return static_cast<uint32_t>(power_percent) * kMaxDuty / 100;
    }

    void ApplyPower(int power_percent) {
        output_power_percent_ = ClampInt(power_percent, 0, 100);

        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, PowerToDuty(output_power_percent_)));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_));
    }

    void ResetPid() {
        pid_integral_ = 0.0f;
        previous_error_ = 0.0f;
    }

    void UpdatePid() {
        current_temperature_c_ = temperature_sensor_.ReadCelsius();

        if (!enabled_ || target_temperature_c_ <= 0) {
            ApplyPower(0);
            return;
        }

        if (!std::isfinite(current_temperature_c_)) {
            ESP_LOGW(TAG, "NTC temperature unavailable, heater output disabled");
            ApplyPower(0);
            return;
        }

        float error = static_cast<float>(target_temperature_c_) - current_temperature_c_;
        pid_integral_ = ClampFloat(pid_integral_ + error, -100.0f, 100.0f);
        float derivative = error - previous_error_;
        previous_error_ = error;

        float pid_output = kp_ * error + ki_ * pid_integral_ + kd_ * derivative;
        int power_percent = static_cast<int>(std::lround(pid_output));
        ApplyPower(ClampInt(power_percent, 0, 100));
    }

    void SetTargetTemperature(int target_temperature_c) {
        target_temperature_c_ = ClampInt(target_temperature_c, 0, max_target_temperature_c_);
        enabled_ = target_temperature_c_ > 0;
        ResetPid();

        if (!enabled_) {
            ApplyPower(0);
            current_temperature_c_ = temperature_sensor_.ReadCelsius();
            return;
        }

        UpdatePid();
    }

    cJSON* BuildStateJson() const {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "device", "heater");
        cJSON_AddNumberToObject(json, "pin", gpio_num_);
        cJSON_AddBoolToObject(json, "enabled", enabled_);
        cJSON_AddNumberToObject(json, "target_temperature_c", target_temperature_c_);
        if (std::isfinite(current_temperature_c_)) {
            cJSON_AddNumberToObject(json, "current_temperature_c", current_temperature_c_);
        } else {
            cJSON_AddNullToObject(json, "current_temperature_c");
        }
        cJSON_AddNumberToObject(json, "pid_output_power", output_power_percent_);
        cJSON_AddNumberToObject(json, "frequency_hz", frequency_hz_);
        return json;
    }

public:
    HeaterController(gpio_num_t gpio_num,
                     uint32_t frequency_hz,
                     ledc_timer_t timer_num,
                     ledc_channel_t channel,
                     NtcTemperatureSensor& temperature_sensor,
                     int max_target_temperature_c,
                     float kp,
                     float ki,
                     float kd)
        : gpio_num_(gpio_num),
          timer_num_(timer_num),
          channel_(channel),
          frequency_hz_(frequency_hz),
          temperature_sensor_(temperature_sensor),
          max_target_temperature_c_(max_target_temperature_c),
          kp_(kp),
          ki_(ki),
          kd_(kd) {
        if (gpio_num_ == GPIO_NUM_NC) {
            return;
        }

        ledc_timer_config_t ledc_timer = {};
        ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
        ledc_timer.freq_hz = frequency_hz_;
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = timer_num_;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

        ledc_channel_config_t ledc_channel = {};
        ledc_channel.channel = channel_;
        ledc_channel.duty = 0;
        ledc_channel.gpio_num = gpio_num_;
        ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_channel.hpoint = 0;
        ledc_channel.timer_sel = timer_num_;
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

        esp_timer_create_args_t timer_config = {};
        timer_config.callback = [](void* arg) {
            static_cast<HeaterController*>(arg)->UpdatePid();
        };
        timer_config.arg = this;
        timer_config.name = "heater_pid";
        ESP_ERROR_CHECK(esp_timer_create(&timer_config, &pid_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(pid_timer_, kPidPeriodUs));

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.heater.get_state",
            "Get the heater thermostat state, including target temperature, current NTC temperature, PID output power, and PWM frequency.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                current_temperature_c_ = temperature_sensor_.ReadCelsius();
                return BuildStateJson();
            });

        mcp_server.AddTool("self.heater.set_target_temperature",
            "Set heater target temperature in Celsius. The ESP32 controls heater PWM locally with PID using the NTC ADC temperature as feedback. Set target_temperature_c to 0 to turn heating off.",
            PropertyList({
                Property("target_temperature_c", kPropertyTypeInteger, 0, max_target_temperature_c_)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SetTargetTemperature(properties["target_temperature_c"].value<int>());
                return BuildStateJson();
            });
    }

    ~HeaterController() {
        if (pid_timer_ != nullptr) {
            esp_timer_stop(pid_timer_);
            esp_timer_delete(pid_timer_);
        }
    }
};

#endif // __HEATER_CONTROLLER_H__
