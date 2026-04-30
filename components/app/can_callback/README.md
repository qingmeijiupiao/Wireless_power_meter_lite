# can_callback

CAN 外设初始化与回调注册模块，在 `CanCallback` 命名空间内集中管理 CAN 外设初始化及所有消息接收回调的注册与实现。

## 模块特点

- **一站式初始化**：`init()` 内完成 CAN 电阻使能、TWAI 驱动初始化及回调注册
- **集中管理**：所有回调在 `init()` 中统一注册，添加回调只需修改一处
- **Lambda 实现**：回调处理函数以 lambda 表达式内联，回调定义与实现紧邻，可读性高
- **标准注释**：每条回调遵循统一注释模板，包含 ID、用途、参数和注意事项
- **NVS 可配**：CAN 波特率和设备 ID 通过 `NVS_DATA` 持久化，运行时可修改并重启生效
- **自动扩展帧**：设备 ID > 0x700 时自动切换为扩展帧格式

## 集成与使用

```cpp
#include "can_callback.h"

// 一站式初始化：使能电阻、TWAI驱动、注册回调
CanCallback::init();

// 获取 CAN 总线引用（用于发送等操作）
HXC_TWAI& bus = CanCallback::get_can_bus();
bus.send(&msg);

// 运行时修改 CAN 配置（持久化到 NVS，重启后生效）
CanCallback::CAN_BAUDRATE = 500_Kbps;
CanCallback::CAN_ID = 0x500;
```

## 添加新回调

在 `can_callback.cpp` 的 `init()` 函数中，按以下模板追加：

```cpp
/**
 * @brief  0x<ID> - <简要描述>
 * @usage  收到 ID=0x<ID> 的 CAN 帧时执行
 * @param  msg - CAN 消息指针
 * @note   <注意事项>
 */
can_bus.add_can_receive_callback_func(0x<ID>,
    [](HXC_CAN_message_t* msg) {
        // 回调实现
    });
```

### 示例：添加 ID 为 0x456 的回调

```cpp
/**
 * @brief  0x456 - 电机状态反馈
 * @usage  收到 ID=0x456 的 CAN 帧时更新电机状态
 * @param  msg - CAN 消息指针
 * @note   数据格式: [0-1] 转速, [2-3] 电流
 */
can_bus.add_can_receive_callback_func(0x456,
    [](HXC_CAN_message_t* msg) {
        uint16_t rpm = (msg->data[0] << 8) | msg->data[1];
        uint16_t current = (msg->data[2] << 8) | msg->data[3];
        ESP_LOGI("Motor", "RPM=%d Current=%dmA", rpm, current);
    });
```

## 回调 ID 枚举

| 值 | 名称 | 说明 |
|----|------|------|
| `0x00` | `CALLBACK_PING` | 心跳 Ping |
| `0x01` | `CALLBACK_GET_STATE` | 获取状态 |
| `0x02` | `CALLBACK_SET_OUTPUT` | 设置输出 |

回调 ID 与设备 CAN ID 相加得到实际 CAN 帧标识符：`CAN_ID + CALLBACK_ID`。

## 数据结构

### CALLBACK_GET_STATE_DATA_t（8 字节）

| 字段 | 类型 | 说明 |
|------|------|------|
| `voltage_mV` | `uint16_t` | 电压，单位 mV |
| `current_mA` | `int16_t` | 电流绝对值，单位 mA |
| `Board_temperature` | `int8_t` | 板载温度，单位 1°C |
| `Chip_temperature` | `int8_t` | 芯片温度，单位 1°C |
| `output_state` | `uint8_t:1` | 输出状态，0: 关闭 1: 开启 |
| `current_direction` | `uint8_t:1` | 电流方向，0: 正向 1: 反向 |
| `reserved` | `uint8_t:6` | 保留位 |
| `UVP_flag` | `ProtectState_t:2` | 低压保护状态 |
| `OVP_flag` | `ProtectState_t:2` | 高压保护状态 |
| `OTP_flag` | `ProtectState_t:2` | 过温保护状态 |
| `OCP_flag` | `ProtectState_t:2` | 过流保护状态 |

### CALLBACK_SET_OUTPUT_DATA_t

| 字段 | 类型 | 说明 |
|------|------|------|
| `output_state` | `bool` | `true` 开启输出，`false` 关闭输出 |

## NVS 配置变量

| 变量 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `CAN_BAUDRATE` | `NVS_DATA<uint32_t>` | `1_Mbps` | CAN 波特率，持久化存储 |
| `CAN_ID` | `NVS_DATA<uint32_t>` | `0x400` | 设备 CAN ID，>0x700 时使用扩展帧 |

## 已注册回调

| ID | 说明 | 数据格式 |
|----|------|----------|
| `CAN_ID+0x00` (PING) | 心跳回应，原样返回接收帧 | 同请求帧 |
| `CAN_ID+0x01` (GET_STATE) | 返回当前系统状态 | `CALLBACK_GET_STATE_DATA_t` |
| `CAN_ID+0x02` (SET_OUTPUT) | 根据数据控制输出开关 | `CALLBACK_SET_OUTPUT_DATA_t` |
| `-1` | 调试用 Catch-All，打印所有帧 | 无特定格式 |

## 环境与依赖

- **软件**：ESP-IDF v5.x、C++11
- **组件依赖**：`HXC_TWAI`、`HXC_NVS`、`hardware`、`cpp_gpio_driver`、`protect`、`global_state`、`power_output`
