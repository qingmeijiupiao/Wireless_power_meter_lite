# boot_diagnostics

应用启动诊断组件，负责将启动阶段、固件信息、硬件配置和运行期摘要写入黑匣子。

## API

| API | 说明 |
| --- | --- |
| `append_stage(stage)` | 记录当前启动阶段 |
| `append_hardware_config_failure(err)` | 记录硬件配置初始化失败 |
| `append_system_boot_start()` | 记录系统启动事件 |
| `append_early()` | 记录固件、Flash、MAC、CAN、WiFi、校准和保护阈值 |
| `append_runtime()` | 记录网络状态和 INA226 原始值 |

该组件只负责诊断记录。初始化顺序仍由 `main/app_main.cpp` 编排。
