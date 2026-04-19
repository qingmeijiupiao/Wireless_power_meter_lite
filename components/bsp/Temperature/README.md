# Temperature

温度采集模块，提供两种温度传感器封装：NTC 热敏电阻（外置）与 ESP 芯片内置温度传感器。

## 模块特点

### NTC（`NTCTemperatureSensor.hpp`）
- **ADC 采集 + 校准**：基于 `adc_oneshot` + `adc_cali_curve_fitting` 获取经校准的 mV 值
- **查表 + 插值**：NTC 阻值-温度查表，配合 `Interp` 模块做非等距插值
- **一阶低通滤波**：α=0.1 一阶 IIR 滤波，抑制噪声

### ESP 芯片内温（`ESPChipTemperatureSensor.hpp`）
- **自动量程切换**：5 档温度范围，接近边界时自动切换以优化精度
- **精度**：默认范围（-10°C ~ 80°C）±1°C

## 集成与使用

```cpp
#include "NTCTemperatureSensor.hpp"
#include "ESPChipTemperatureSensor.hpp"

NTC::init(ADC_CHANNEL_0);
int16_t ntc_temp = NTC::getTemperature();  // 0.01°C 单位

TemperatureSensor_t chip_temp;
chip_temp.init();
float t = chip_temp.getTemperature();       // °C 单位
```

## API 参考

| API | 说明 |
|-----|------|
| `NTC::init(channel, unit_id)` | 初始化 ADC + 校准 + 插值表 |
| `NTC::getTemperature()` | 返回 NTC 温度，0.01°C 单位 |
| `TemperatureSensor_t::init()` | 初始化芯片内置温度传感器 |
| `TemperatureSensor_t::getTemperature()` | 返回芯片温度，°C 单位 |

## 环境与依赖

- **硬件**：NTC 热敏电阻接 ADC 通道
- **软件**：ESP-IDF v5.x
- **组件依赖**：`esp_adc`、`Interp`、`esp_driver_tsens`、`esp_hal_ana_conv`
