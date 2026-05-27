# 加热器目标温度控制

`bread-compact-wifi` 板型使用 `GPIO18` 输出加热器 PWM，并使用 NTC 接到 ADC0 读取当前温度。ESP32 本地后台控制定时器每 100ms 采样温度，并在加热开启后运行 PID 计算 PWM 占空比；AI 通过 MCP 设置目标温度或读取缓存状态。

板级配置在 `main/boards/bread-compact-wifi/config.h`：

```cpp
#define HEATER_GPIO        GPIO_NUM_18
#define HEATER_PWM_FREQ_HZ 1000
#define HEATER_PWM_TIMER   LEDC_TIMER_2
#define HEATER_PWM_CHANNEL LEDC_CHANNEL_1

// NTC temperature sensor on ADC0. Wiring: 3V3 -> series resistor -> ADC0 -> NTC -> GND.
#define HEATER_NTC_ADC_UNIT              ADC_UNIT_1
#define HEATER_NTC_ADC_CHANNEL           ADC_CHANNEL_0
#define HEATER_NTC_SERIES_RESISTOR_OHM   10000.0f
#define HEATER_NTC_NOMINAL_RESISTOR_OHM  10000.0f
#define HEATER_NTC_NOMINAL_TEMPERATURE_C 25.0f
#define HEATER_NTC_BETA                  3435.0f

#define HEATER_MAX_TARGET_TEMPERATURE_C  120
#define HEATER_PID_KP                    8.0f
#define HEATER_PID_KI                    0.25f
#define HEATER_PID_KD                    2.0f
```

已注册 MCP 工具：

- `self.heater.get_state`：读取目标温度、NTC 当前温度、PID 输出功率和 PWM 频率
- `self.heater.set_target_temperature`：设置目标温度，参数 `target_temperature_c` 范围为 `0` 到 `HEATER_MAX_TARGET_TEMPERATURE_C`，设为 `0` 表示关闭加热

示例：设置目标温度为 60 摄氏度。

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.heater.set_target_temperature",
    "arguments": {
      "target_temperature_c": 60
    }
  },
  "id": 1
}
```

示例：关闭加热。

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.heater.set_target_temperature",
    "arguments": {
      "target_temperature_c": 0
    }
  },
  "id": 2
}
```

注意：加热器属于高风险外设，`GPIO18` 应通过合适的 MOSFET、继电器或固态继电器驱动负载，不要用 ESP32 引脚直接带加热器。首次上电建议限流验证 NTC 接线、温度读数方向和 PID 参数。
