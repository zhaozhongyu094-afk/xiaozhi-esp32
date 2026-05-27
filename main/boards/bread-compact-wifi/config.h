#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_adc/adc_oneshot.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true


// Heater thermostat output and temperature feedback
#define HEATER_GPIO        GPIO_NUM_18
#define HEATER_PWM_FREQ_HZ 1000
#define HEATER_PWM_TIMER   LEDC_TIMER_2
#define HEATER_PWM_CHANNEL LEDC_CHANNEL_1

// NTC divider: 3V3 -> HEATER_TEMP_SERIES_RESISTOR_OHM -> ADC -> NTC -> GND
#define HEATER_TEMP_ADC_UNIT                ADC_UNIT_1
#define HEATER_TEMP_ADC_CHANNEL             ADC_CHANNEL_0  // GPIO1 on ESP32-S3
#define HEATER_TEMP_SERIES_RESISTOR_OHM     10000.0f
#define HEATER_TEMP_NOMINAL_RESISTOR_OHM    10000.0f
#define HEATER_TEMP_NOMINAL_TEMPERATURE_C   25.0f
#define HEATER_TEMP_BETA                    3950.0f
#define HEATER_MAX_TARGET_TEMPERATURE_C     120

// PID output maps to 0-100% PWM. Tune these values for the actual heater and thermal mass.
#define HEATER_PID_KP 8.0f
#define HEATER_PID_KI 0.25f
#define HEATER_PID_KD 2.0f

#endif // _BOARD_CONFIG_H_
