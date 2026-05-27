#ifndef __HEATER_CONTROLLER_H__
#define __HEATER_CONTROLLER_H__

#include "mcp_server.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_err.h>

class HeaterController {
private:
    static constexpr uint32_t kMaxDuty = 8191;

    gpio_num_t gpio_num_;
    ledc_timer_t timer_num_;
    ledc_channel_t channel_;
    uint32_t frequency_hz_;
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
        enabled_ = power_percent_ > 0;

        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, channel_, PowerToDuty(power_percent_)));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, channel_));
    }

    cJSON* BuildStateJson() const {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "device", "heater");
        cJSON_AddNumberToObject(json, "pin", gpio_num_);
        cJSON_AddBoolToObject(json, "enabled", enabled_);
        cJSON_AddNumberToObject(json, "power", power_percent_);
        cJSON_AddNumberToObject(json, "frequency_hz", frequency_hz_);
        return json;
    }

public:
    HeaterController(gpio_num_t gpio_num,
                     uint32_t frequency_hz,
                     ledc_timer_t timer_num,
                     ledc_channel_t channel)
        : gpio_num_(gpio_num),
          timer_num_(timer_num),
          channel_(channel),
          frequency_hz_(frequency_hz) {
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

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.heater.get_state",
            "Get the heater state, including GPIO pin, on/off state, PWM power percentage, and PWM frequency.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                return BuildStateJson();
            });

        mcp_server.AddTool("self.heater.turn_on",
            "Turn on the heater GPIO output at 100% PWM duty. Use this when the user asks to start, enable, or turn on the heater.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ApplyPower(100);
                return BuildStateJson();
            });

        mcp_server.AddTool("self.heater.turn_off",
            "Turn off the heater GPIO output. Use this when the user asks to stop, disable, or turn off the heater.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                ApplyPower(0);
                return BuildStateJson();
            });

        mcp_server.AddTool("self.heater.set_level",
            "Set the heater GPIO logic level. Level 1 means GPIO high/on, level 0 means GPIO low/off.",
            PropertyList({
                Property("level", kPropertyTypeInteger, 0, 1)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                ApplyPower(properties["level"].value<int>() == 1 ? 100 : 0);
                return BuildStateJson();
            });

        mcp_server.AddTool("self.heater.set_power",
            "Set heater PWM power percentage on the heater GPIO. Use 0 for off and 100 for full power.",
            PropertyList({
                Property("power", kPropertyTypeInteger, 0, 100)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                ApplyPower(properties["power"].value<int>());
                return BuildStateJson();
            });
    }
};

#endif // __HEATER_CONTROLLER_H__
