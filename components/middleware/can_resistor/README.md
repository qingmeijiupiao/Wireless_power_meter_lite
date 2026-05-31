# can_resistor

CAN 终端电阻控制中间件。封装终端电阻使能 GPIO、状态持久化和
`GlobalState` 同步，供 CAN 回调和屏幕设置页复用。

## 行为

- `CanResistor::instance()` 返回单例控制器。
- `init()` 初始化 GPIO，并从 `can_term` NVS Key 恢复上次状态。
- `set()` 和 `toggle()` 修改 GPIO 后立即写入 NVS。
- GPIO 状态变化时同步更新 `flags.bits.can_resistor_enabled`。

## API

| API | 说明 |
|-----|------|
| `instance()` | 获取单例控制器 |
| `init(gpio_num)` | 初始化 GPIO 并恢复持久化状态 |
| `set(enabled)` | 设置状态并持久化 |
| `toggle()` | 切换状态并持久化 |
| `get()` | 获取当前 GPIO 状态 |

## NVS Key

| Key | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `can_term` | blob(uint8_t) | `0` | `1` 表示接入 CAN 终端电阻 |

## 依赖

- `cpp_gpio_driver`
- `HXC_NVS`
- `global_state`
