# global_state

全局状态共享模块，提供系统运行时数据的单例访问点，供其他模块（如 protect、UI、通信）统一读写电压、电流、温度及保护状态等关键量。

## 模块特点

- **单例引用**：`get_global_state()` 返回全局唯一 `GlobalState` 引用，零拷贝共享
- **位域压缩**：`GlobalState_bit` 使用位域管理布尔标志，4 字节容纳 32 个开关量
- **`packed` 对齐**：结构体 `__attribute__((packed))` 保证内存布局一致，适合 DMA / 持久化场景
- **与 protect 联动**：内嵌 `protect_states_t` 直接承载保护状态

## 架构与数据流

```mermaid
flowchart LR
    ULP["LP 核共享变量<br/>ulp_voltage_uv / ulp_current_uA"] --> Timer["update_main_state<br/>5ms FreeRTOS Timer"]
    TMP["TMP235_t<br/>板温 0.01°C"] --> Timer
    CHIP["ESPChipTemperatureSensor_t<br/>芯片温度 °C"] --> Timer
    Timer --> GS["GlobalState<br/>全局静态实例"]

    GS --> Protect["protect<br/>20Hz 状态机"]
    GS --> Screen["screen<br/>60FPS 渲染"]
    GS --> CAN["can_callback<br/>状态帧打包"]
    GS --> BlackBox["blackbox_structured<br/>结构化快照"]
    Power["PowerOutput / CAN resistor<br/>GPIO on_change 回调"] --> GS
```

```mermaid
classDiagram
    class GlobalState {
        +uint16_t voltage_mV
        +int32_t current_uA
        +int16_t board_temperature
        +int16_t chip_temperature
        +protect_states_t protect_states
        +GlobalState_bit global_state_bits
    }
    class GlobalState_bit {
        +uint32_t raw
        +out_put_state : 1
        +can_resistor_state : 1
        +protect_bypassed : 1
        +reverse : 29
    }
    class protect_states_t {
        +uint8_t protect_states_raw
        +temperature_protect_state : 2
        +high_voltage_protect_state : 2
        +low_voltage_protect_state : 2
        +current_protect_state : 2
    }
    GlobalState *-- GlobalState_bit
    GlobalState *-- protect_states_t
```

## 数据结构

| 字段 | 类型 | 说明 |
|------|------|------|
| `voltage_mV` | `uint16_t` | 电压，单位 mV |
| `current_uA` | `int32_t` | 电流，单位 μA |
| `board_temperature` | `int16_t` | 板载温度，单位 0.01°C |
| `chip_temperature` | `int16_t` | 芯片内部温度，单位 0.01°C |
| `protect_states` | `protect_states_t` | 保护状态位域 |
| `global_state_bits` | `GlobalState_bit` | 通用状态位域（输出、CAN 终端电阻、保护旁路等） |

## 集成与使用

```cpp
#include "global_state.h"

auto& state = get_global_state();
state.voltage_mV = 12000;
state.current_uA = 1500000;
bool out = state.global_state_bits.state_bit.out_put_state;
```

## 环境与依赖

- **软件**：ESP-IDF v6.0+、C++11
- **组件依赖**：`protect`（提供 `protect_states_t` 类型定义）
