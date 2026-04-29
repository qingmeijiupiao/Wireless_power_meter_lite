# can_callback

CAN 外设初始化与回调注册模块，在 `CanCallback` 命名空间内集中管理 CAN 外设初始化及所有消息接收回调的注册与实现。

## 模块特点

- **一站式初始化**：`init()` 内完成 CAN 电阻使能、TWAI 驱动初始化及回调注册
- **集中管理**：所有回调在 `init()` 中统一注册，添加回调只需修改一处
- **Lambda 实现**：回调处理函数以 lambda 表达式内联，回调定义与实现紧邻，可读性高
- **标准注释**：每条回调遵循统一注释模板，包含 ID、用途、参数和注意事项

## 集成与使用

```cpp
#include "can_callback.h"

// 一站式初始化：使能电阻、TWAI驱动、注册回调
CanCallback::init();

// 获取 CAN 总线引用（用于发送等操作）
HXC_TWAI& bus = CanCallback::get_can_bus();
bus.send(&msg);
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

## 已注册回调

| ID | 说明 | 数据格式 |
|----|------|----------|
| `0x123` | 测试回调 | 无特定格式 |

## 环境与依赖

- **软件**：ESP-IDF v5.x、C++11
- **组件依赖**：`HXC_TWAI`、`hardware`、`cpp_gpio_driver`
