# shell_command

Shell 命令注册模块，在 `ShellCommand` 命名空间内集中管理所有应用层命令的注册与实现。

## 模块特点

- **集中管理**：所有命令在 `init()` 中统一注册，添加命令只需修改一处
- **Lambda 实现**：命令处理函数以 lambda 表达式内联，命令定义与实现紧邻，可读性高
- **标准注释**：每条命令遵循统一注释模板，包含用途、用法、参数和注意事项
- **WiFi/Web 控制入口**：通过 `wifi` 命令统一调用 `WifiService` 与 `WebBackend`，可查询状态、启动/停止网络、连接 STA、进入 AP 配网和配置启动开关
- **黑匣子维护入口**：通过 `blackbox` 命令查询状态、拉取日志和同步清空日志分区

## 注册与执行时序

```mermaid
sequenceDiagram
    participant Main as app_main
    participant SC as ShellCommand::init()
    participant Shell as Shell 单例
    participant Console as esp_console
    participant App as 应用模块

    Main->>SC: 初始化命令模块
    SC->>Shell: Shell::instance().init()
    SC->>Shell: register_command(...)
    Shell->>Console: esp_console_cmd_register
    Console->>Shell: 用户输入命令
    Shell->>SC: 执行对应 lambda
    SC->>App: 调用 ST7735 / CAN / PowerOutput / protect / WifiService / WebBackend / Blackbox / NVS
```

```mermaid
flowchart TD
    Base["基础命令<br/>reboot / timestamp / version"] --> Runtime["运行控制<br/>backlight / output / protect"]
    Runtime --> Network["网络与 Web<br/>wifi status/ip/on/off/connect/ap/boot/clear"]
    Network --> Bus["通信参数<br/>can_baudrate / can_id"]
    Bus --> Measure["测量诊断<br/>get_data / ina226_register / calibration_params"]
    Measure --> Diagnose["设备诊断<br/>rtos_stats / blackbox status/dump/clear"]
    Diagnose --> Factory["factory_mode"]
    Factory --> Hidden["注册工厂校准命令<br/>calibration_basek<br/>calibration_current_temperatureK<br/>calibration_current_points<br/>calibration_clear"]
```

## 集成与使用

```cpp
#include "shell_command.h"

// 内部会调用 Shell::instance().init()
ShellCommand::init();
```

## 添加新命令

在 `shell_command.cpp` 的 `init()` 函数中，按以下模板追加：

```cpp
/**
 * @brief  <命令名> - <简要描述>
 * @usage  <命令名> [参数列表]
 * @param  <参数1> - <参数说明>
 * @note   <注意事项>
 */
shell.register_command(ShellCommand_t("<命令名>", "<help文本>", "<hint文本>",
    [](int argc, char** argv) -> int {
        // 命令实现
        return 0;
    }));
```

### 示例：添加带参数的命令

```cpp
/**
 * @brief  echo - 回显输入文本
 * @usage  echo <text>
 * @param  text - 要回显的文本
 */
shell.register_command(ShellCommand_t("echo", "Echo input text", "<text>",
    [](int argc, char** argv) -> int {
        for (int i = 1; i < argc; i++) {
            printf("%s ", argv[i]);
        }
        printf("\n");
        return 0;
    }));
```

## 已注册命令

| 命令 | 说明 | 参数 |
|------|------|------|
| `reboot` | 重启设备 | 无 |
| `timestamp` | 获取系统时间戳(微秒) | 无 |
| `version` | 获取固件版本号与编译时间 | 无 |
| `backlight` | 设置/查询屏幕背光亮度 | `[0-255]` |
| `can_baudrate` | 设置/查询 CAN 波特率配置值；修改后重启生效 | `[baudrate]` |
| `can_id` | 设置/查询 CAN ID 配置值，重启或重新初始化后用于回调注册 | `[id]` |
| `get_data` | 获取当前电压、电流、板温 | 无 |
| `meter` | 查询或重置 UI/Web/Shell 共用的电量计量会话，输出相对累计值、LP Core 自启动累计值、计量时间、系统时间和实时功率 | `[status|reset]` |
| `blackbox` | 查询黑匣子状态、拉取日志或同步清空日志分区 | `[status|dump [count\|all]|pull [count\|all]|clear]` |
| `output` | 设置/查询输出状态 | `[0/1]` |
| `protect` | 设置/查询保护阻断状态和各保护通道 | `[0/1]` |
| `wifi` | 管理 WiFi/Web，支持查询、启动、停止、连接 STA、进入 AP 配网、设置启动开关、清除保存凭据 | `status|ip|on|off|connect <ssid> [password]|ap|boot [0/1]|clear` |
| `ina226_register` | 查看 INA226 原始寄存器指针值 | 无 |
| `calibration_params` | 查看电流校准参数 | 无 |
| `factory_mode` | 进入工厂模式，旁路保护并注册校准写入命令 | 无 |

### `wifi` 子命令

| 子命令 | 说明 |
|--------|------|
| `wifi` / `wifi status` / `wifi ip` | 打印当前模式、底层 WiFi 状态、Web 是否运行、IP、已保存 SSID、AP SSID、STA/AP MAC、启动开关和最近错误 |
| `wifi on` | 初始化并启动 WebBackend，然后按 NVS 配置启动 WiFiService；优先 STA，失败或未配置时进入 AP 配网 |
| `wifi off` | 停止 WiFiService 管理的网络功能 |
| `wifi connect <ssid> [password]` | 启动 WebBackend 并连接指定 STA，成功后保存到 NVS |
| `wifi ap` | 启动 WebBackend 并切换到 AP 配网模式 |
| `wifi boot` | 查询启动时是否自动启用 WiFi/Web |
| `wifi boot <0|1>` | 设置启动时是否自动启用 WiFi/Web |
| `wifi clear` | 清除已保存的 STA SSID 和密码 |

### `blackbox` 子命令

| 子命令 | 说明 |
|--------|------|
| `blackbox` / `blackbox status` | 打印黑匣子启用状态和已落盘的原始记录数 |
| `blackbox dump [count]` / `blackbox pull [count]` | 先同步在途记录，再按从新到旧顺序输出指定数量的逻辑日志；默认输出最新 100 条。字符串自动拼接且合并后算一条，结构化记录输出十六进制 payload |
| `blackbox dump all` / `blackbox pull all` | 明确请求全量输出所有已落盘日志 |
| `blackbox clear` | 同步清空日志分区；完成后保留一条 `"[Blackbox]: reset"` 标记 |

## 环境与依赖

- **软件**：ESP-IDF v6.0+、C++11
- **组件依赖**：`shell`、`hardware`、`st7735_driver`、`can_callback`、`current_calibration`、`global_state`、`energy_meter`、`power_output`、`protect`、`wifi_service`、`web_backend`、`blackbox`、`esp_app_desc`
