# 加热器 GPIO/PWM 控制

当前 `bread-compact-wifi` 板型默认把 `GPIO18` 作为加热器输出，并通过 MCP 工具暴露给 AI。用户说“打开加热器”“把加热器功率调到 30%”“关闭加热器”时，云端 AI 会把自然语言转换成设备端工具调用。

板级配置在 `main/boards/bread-compact-wifi/config.h`：

```cpp
#define HEATER_GPIO        GPIO_NUM_18
#define HEATER_PWM_FREQ_HZ 1000
#define HEATER_PWM_TIMER   LEDC_TIMER_2
#define HEATER_PWM_CHANNEL LEDC_CHANNEL_1
```

已注册工具：

- `self.heater.get_state`：读取加热器 GPIO、开关状态、PWM 功率百分比和频率
- `self.heater.turn_on`：GPIO18 输出高电平，相当于 100% PWM
- `self.heater.turn_off`：GPIO18 输出低电平，相当于 0% PWM
- `self.heater.set_level`：直接设置 GPIO 逻辑电平，`level` 为 `0` 或 `1`
- `self.heater.set_power`：输出 PWM，`power` 为 `0` 到 `100`

示例：设置 30% PWM 功率。

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.heater.set_power",
    "arguments": {
      "power": 30
    }
  },
  "id": 1
}
```

示例：直接拉高 GPIO18。

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.heater.set_level",
    "arguments": {
      "level": 1
    }
  },
  "id": 2
}
```

注意：加热器属于高风险外设，GPIO18 应通过合适的 MOSFET、继电器或固态继电器驱动负载，不要用 ESP32 引脚直接带加热器。
