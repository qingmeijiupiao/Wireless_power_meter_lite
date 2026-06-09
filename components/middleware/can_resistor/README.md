# can_resistor

CAN 终端电阻控制中间件。封装终端电阻使能 GPIO、状态持久化和
`GlobalState` 同步，供 CAN 回调和屏幕设置页复用。

## 行为

- `CanResistor::instance()` 返回单例控制器。
- `init()` 初始化 GPIO，并从 `can_term` NVS Key 恢复上次状态。
- `set()` 和 `toggle()` 修改 GPIO 后立即写入 NVS；持久化失败时返回错误并回滚 GPIO。
- GPIO 状态变化时同步更新 `flags.bits.can_resistor_enabled`。

> 架构边界：该同步行为使本组件依赖应用层 `global_state`，是当前唯一明确的
> `middleware -> app` 反向依赖。功能行为暂时保持不变，后续可通过状态回调或将
> 组件归入 `app` 层消除。

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

<!-- dependency-links:start -->
## 依赖导航

工程内直接依赖：

- [`global_state`](../../app/global_state/README.md)（`app`）
- [`cpp_gpio_driver`](../../bsp/cpp_gpio_driver/README.md)（`bsp`）
- [`HXC_NVS`](../../bsp/HXC_NVS/README.md)（`bsp`）

> 本节按当前 `CMakeLists.txt` 的 `REQUIRES` / `PRIV_REQUIRES` 维护。
<!-- dependency-links:end -->
