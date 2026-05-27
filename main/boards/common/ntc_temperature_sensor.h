#ifndef __NTC_TEMPERATURE_SENSOR_H__
#define __NTC_TEMPERATURE_SENSOR_H__

#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>
#include <esp_log.h>

#include <cmath>

class NtcTemperatureSensor {
private:
    static constexpr const char* TAG = "NtcTemperatureSensor";
    static constexpr int kAdcMaxRaw = 4095;
    static constexpr float kKelvinOffset = 273.15f;

    adc_unit_t adc_unit_;
    adc_channel_t adc_channel_;
    float series_resistor_ohm_;
    float nominal_resistor_ohm_;
    float nominal_temperature_c_;
    float beta_;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;

public:
    NtcTemperatureSensor(adc_unit_t adc_unit,
                         adc_channel_t adc_channel,
                         float series_resistor_ohm,
                         float nominal_resistor_ohm,
                         float nominal_temperature_c,
                         float beta)
        : adc_unit_(adc_unit),
          adc_channel_(adc_channel),
          series_resistor_ohm_(series_resistor_ohm),
          nominal_resistor_ohm_(nominal_resistor_ohm),
          nominal_temperature_c_(nominal_temperature_c),
          beta_(beta) {
        adc_oneshot_unit_init_cfg_t adc_init_config = {};
        adc_init_config.unit_id = adc_unit_;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init_config, &adc_handle_));

        adc_oneshot_chan_cfg_t adc_config = {};
        adc_config.bitwidth = ADC_BITWIDTH_DEFAULT;
        adc_config.atten = ADC_ATTEN_DB_12;
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, adc_channel_, &adc_config));
    }

    ~NtcTemperatureSensor() {
        if (adc_handle_ != nullptr) {
            adc_oneshot_del_unit(adc_handle_);
        }
    }

    float ReadCelsius() {
        int raw_sum = 0;
        int samples = 0;

        for (int i = 0; i < 8; ++i) {
            int raw = 0;
            esp_err_t err = adc_oneshot_read(adc_handle_, adc_channel_, &raw);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to read ADC channel %d: %s", adc_channel_, esp_err_to_name(err));
                continue;
            }
            raw_sum += raw;
            ++samples;
        }

        if (samples == 0) {
            return NAN;
        }

        float raw_average = static_cast<float>(raw_sum) / samples;
        if (raw_average < 1.0f) {
            raw_average = 1.0f;
        } else if (raw_average > static_cast<float>(kAdcMaxRaw - 1)) {
            raw_average = static_cast<float>(kAdcMaxRaw - 1);
        }

        // Divider wiring: 3V3 -> series resistor -> ADC -> NTC -> GND.
        float ntc_resistance = series_resistor_ohm_ * raw_average / (kAdcMaxRaw - raw_average);
        float steinhart = std::log(ntc_resistance / nominal_resistor_ohm_) / beta_;
        steinhart += 1.0f / (nominal_temperature_c_ + kKelvinOffset);
        return (1.0f / steinhart) - kKelvinOffset;
    }
};

#endif // __NTC_TEMPERATURE_SENSOR_H__
