#ifndef __HEATER_CONTROLLER_H__
#define __HEATER_CONTROLLER_H__

#include "mcp_server.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>

class HeaterController {
private:
    static constexpr const char* TAG = "HeaterController";
    static constexpr uint32_t kMaxDuty = 8191;
    static constexpr float kKelvinOffset = 273.15f;
    static constexpr int kAdcMaxRaw = 4095;
    static constexpr int64_t kControlPeriodUs = 1000000;

    gpio_num_t gpio_num_;
    ledc_timer_t timer_num_;
    ledc_channel_t channel_;
    uint32_t frequency_hz_;
    adc_unit_t temperature_adc_unit_;
    adc_channel_t temperature_adc_channel_;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    esp_timer_handle_t control_timer_ = nullptr;

    float ntc_series_resistor_ohm_;
    float ntc_nominal_resistor_ohm_;
    float ntc_nominal_temperature_c_;
    float ntc_beta_;
    int max_target_temperature_c_;
    float kp_;
    float ki_;
    float kd_;

    int target_temperature_c_ = 0;
    float current_temperature_c_ = NAN;
    float pid_integral_ = 0.0f;
    float previous_error_ = 0.0f;
    int power_percent_ = 0;
    bool enabled_ = false;

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
        power_percent_ = power_percent;

        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, PowerToDuty(power_percent_)));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_));
    }

    float ReadTemperatureC() {
        if (adc_handle_ == nullptr) {
            return NAN;
        }

        int raw_sum = 0;
        int samples = 0;
        for (int i = 0; i < 8; ++i) {
            int raw = 0;
            esp_err_t err = adc_oneshot_read(adc_handle_, temperature_adc_channel_, &raw);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read temperature ADC: %s", esp_err_to_name(err));
                continue;
            }
            raw_sum += raw;
            ++samples;
        }

        if (samples == 0) {
            return NAN;
        }

        float raw_average = static_cast<float>(raw_sum) / samples;
        raw_average = std::clamp(raw_average, 1.0f, static_cast<float>(kAdcMaxRaw - 1));

        // NTC divider: VCC -> series resistor -> ADC -> NTC -> GND.
        float ntc_resistance = ntc_series_resistor_ohm_ * raw_average / (kAdcMaxRaw - raw_average);
        float steinhart = std::log(ntc_resistance / ntc_nominal_resistor_ohm_) / ntc_beta_;
        steinhart += 1.0f / (ntc_nominal_temperature_c_ + kKelvinOffset);
        return (1.0f / steinhart) - kKelvinOffset;
    }

    void UpdateControl() {
        current_temperature_c_ = ReadTemperatureC();
        if (!enabled_ || target_temperature_c_ <= 0) {
            ApplyPower(0);
            return;
        }

        if (std::isnan(current_temperature_c_)) {
            ESP_LOGW(TAG, "Temperature unavailable, heater output disabled");
            ApplyPower(0);
            return;
        }

        float error = static_cast<float>(target_temperature_c_) - current_temperature_c_;
        pid_integral_ = std::clamp(pid_integral_ + error, -100.0f, 100.0f);
        float derivative = error - previous_error_;
        previous_error_ = error;

        float output = kp_ * error + ki_ * pid_integral_ + kd_ * derivative;
        int output_percent = static_cast<int>(std::lround(output));
        ApplyPower(std::clamp(output_percent, 0, 100));
    }

    void SetTargetTemperature(int target_temperature_c) {
        target_temperature_c_ = target_temperature_c;
        enabled_ = target_temperature_c_ > 0;
        pid_integral_ = 0.0f;
        previous_error_ = 0.0f;
        if (!enabled_) {
            ApplyPower(0);
            return;
        }
        UpdateControl();
    }

    cJSON* BuildStateJson() const {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "device", "heater");
        cJSON_AddNumberToObject(json, "pin", gpio_num_);
        cJSON_AddBoolToObject(json, "enabled", enabled_);
        cJSON_AddNumberToObject(json, "target_temperature_c", target_temperature_c_);
        if (std::isnan(current_temperature_c_)) {
            cJSON_AddNullToObject(json, "current_temperature_c");
        } else {
            cJSON_AddNumberToObject(json, "current_temperature_c", current_temperature_c_);
        }
        cJSON_AddNumberToObject(json, "power", power_percent_);
        cJSON_AddNumberToObject(json, "frequency_hz", frequency_hz_);
        return json;
    }

public:
    HeaterController(gpio_num_t gpio_num,
                     uint32_t frequency_hz,
                     ledc_timer_t timer_num,
                     ledc_channel_t channel,
                     adc_unit_t temperature_adc_unit,
                     adc_channel_t temperature_adc_channel,
                     float ntc_series_resistor_ohm,
                     float ntc_nominal_resistor_ohm,
                     float ntc_nominal_temperature_c,
                     float ntc_beta,
                     int max_target_temperature_c,
                     float kp,
                     float ki,
                     float kd)
        : gpio_num_(gpio_num),
          timer_num_(timer_num),
          channel_(channel),
          frequency_hz_(frequency_hz),
          temperature_adc_unit_(temperature_adc_unit),
          temperature_adc_channel_(temperature_adc_channel),
          ntc_series_resistor_ohm_(ntc_series_resistor_ohm),
          ntc_nominal_resistor_ohm_(ntc_nominal_resistor_ohm),
          ntc_nominal_temperature_c_(ntc_nominal_temperature_c),
          ntc_beta_(ntc_beta),
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

        adc_oneshot_unit_init_cfg_t adc_init_config = {};
        adc_init_config.unit_id = temperature_adc_unit_;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init_config, &adc_handle_));

        adc_oneshot_chan_cfg_t adc_config = {};
        adc_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_config.atten = ADC_ATTEN_DB_12;
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, temperature_adc_channel_, &adc_config));

        esp_timer_create_args_t timer_config = {};
        timer_config.callback = [](void* arg) {
            static_cast<HeaterController*>(arg)->UpdateControl();
        };
        timer_config.arg = this;
        timer_config.name = "heater_pid";
        ESP_ERROR_CHECK(esp_timer_create(&timer_config, &control_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(control_timer_, kControlPeriodUs));

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.heater.get_state",
            "Get the heater thermostat state, including current temperature, target temperature, PID output power, and PWM frequency.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                current_temperature_c_ = ReadTemperatureC();
                return BuildStateJson();
            });

        mcp_server.AddTool("self.heater.set_target_temperature",
            "Set the heater target temperature in Celsius. The ESP32 controls PWM locally with PID. Use target_temperature_c=0 to turn heating off.",
            PropertyList({
                Property("target_temperature_c", kPropertyTypeInteger, 0, max_target_temperature_c_)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                SetTargetTemperature(properties["target_temperature_c"].value<int>());
                return BuildStateJson();
            });
    }

    ~HeaterController() {
        if (control_timer_ != nullptr) {
            esp_timer_stop(control_timer_);
            esp_timer_delete(control_timer_);
        }
        if (adc_handle_ != nullptr) {
            adc_oneshot_del_unit(adc_handle_);
        }
    }
};

#endif // __HEATER_CONTROLLER_H__
