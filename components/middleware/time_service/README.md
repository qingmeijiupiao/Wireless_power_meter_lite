# time_service

`time_service` 是中间件层时间同步服务。组件使用 ESP-IDF `esp_netif_sntp`
正式接口维护系统 UTC 时间，并通过 POSIX 时区 `CST-8` 提供 UTC+8 本地时间。

## 模块特点

- 组件保持独立公开接口，由 `WifiService::init()` 在任何 STA 连接发起前调用
  `TimeService::init()`。
- 使用 5 个 NTP 端点：`0.pool.ntp.org`、`time.cloudflare.com`、`time.nist.gov`、
  `1.pool.ntp.org`、`2.pool.ntp.org`。三类来源均使用标准 UTC，避免混用 leap-smear
  与非 leap-smear 时间源。
- 使用平滑校时模式。首次偏差超过 SDK 阈值时，ESP-IDF 会自动立即校准。
- lwIP 每小时自动校时；STA 获取 IP 后主动重启一次 SNTP 请求。
- 事件回调只投递轻量队列，后台任务负责格式化时间和写黑匣子。
- 未完成首次同步时，查询接口不会把系统初始时间误报为有效本地时间。

## 黑匣子事件

每次成功校时输出三条短 `ESP_LOGW`，由应用层 `blackbox_service` 安装的全局日志钩子
自动归档。三条日志使用相同的 `unix_s` 作为 CSV 对齐键，分别保存原始 Unix 时间戳、
UTC+0 和 UTC+8：

```text
[W][TimeService] sync raw unix_s=1780389000 unix_us=123456
[W][TimeService] sync utc unix_s=1780389000 iso=2026-06-02T08:30:00Z
[W][TimeService] sync local unix_s=1780389000 iso=2026-06-02T16:30:00+08:00 timezone=CST-8
```

失败只输出 `ESP_LOGI`，不触发黑匣子归档。底层黑匣子记录头仍保留原有 uptime
时间戳语义，历史格式不迁移。

## API

| API | 说明 |
|-----|------|
| `init()` | 初始化时区、SNTP、事件订阅和后台任务 |
| `is_synchronized()` | 是否至少成功同步过一次 |
| `now_utc()` | 当前 UTC Unix 时间戳 |
| `get_local_time(out)` | 首次同步后返回 UTC+8 本地时间 |
| `last_sync_utc()` | 最近一次成功同步的 UTC Unix 时间戳 |

## 配置

| 配置 | 值 | 说明 |
|------|----|------|
| `CONFIG_LWIP_SNTP_MAX_SERVERS` | `5` | 允许配置 5 个 NTP 服务器 |
| `CONFIG_LWIP_SNTP_UPDATE_DELAY` | `3600000` | 每小时自动校时 |
| `CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC_HRT` | `y` | 断网期间由 RTC 与高精度计时维持系统时间 |

## 依赖

`esp_netif`、`esp_event`、`freertos`、`log`
