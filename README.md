# Wireless Power Meter Lite

无线功率计 Lite — 基于 ESP32-C6 的紧凑型无线功率测量、显示、保护与联网控制设备。

## 功能概览

- 电压/电流/功率实时测量：INA226 由 LP 核采样，主程序读取共享状态。
- 输出保护：支持过温、过压、欠压、过流保护，并与输出控制联动。
- 本地显示与控制：ST7735S 160x80 TFT 显示，按键与串口 Shell 调试。
- 通信与联网：CAN(TWAI) 通信，WiFi STA/AP 配网，Web 页面与 REST API。
- 数据与配置：NVS 持久化配置，Flash 循环日志，结构化黑匣子记录。
- 固件布局：预留 OTA 双 APP 分区，当前在线 OTA 流程尚未实现。

## 硬件平台

| 项目 | 规格 |
|------|------|
| 主控 | ESP32-C6 |
| 功率采样 | INA226 (I2C, LP 核驱动) |
| 温度采样 | TMP235 (ADC) + ESP32-C6 片内温度传感器 |
| 显示屏 | ST7735S 160x80 TFT (SPI) |
| 通信接口 | CAN (TWAI) / WiFi |
| 本地输入 | 主按键 / 侧按键 |

## 分区表

| 分区 | 类型 | 偏移 | 大小 | 说明 |
|------|------|------|------|------|
| nvs | data | 0x9000 | 80KB | NVS 键值存储 |
| otadata | data | 0x1D000 | 8KB | OTA 状态数据 |
| app0 | app(ota_0) | 0x20000 | 1280KB | 应用程序分区 A |
| app1 | app(ota_1) | 0x160000 | 1280KB | 应用程序分区 B |
| blackbox | data | 0x2A0000 | 1408KB | 黑匣子日志分区 |

## 项目结构

```text
├── CMakeLists.txt              # 顶层构建配置与版本号注入
├── partitions.csv              # 分区表
├── sdkconfig                   # ESP-IDF 项目配置
├── scripts/                    # 构建、资源生成和固件合并脚本
├── main/                       # app_main 与 LP 核程序
├── components/
│   ├── app/                    # 应用层组件
│   ├── bsp/                    # 板级支持包
│   ├── middleware/             # 通用中间件
│   ├── assets/                 # 字体、UI、Web 静态资源
│   └── common/                 # 通用算法/库
├── .github/workflows/          # CI/CD
└── .devcontainer/              # VS Code Dev Container
```

## 模块文档

模块内部设计、API、命令、阈值、路由等细节统一维护在对应模块 README 中，主 README 只保留导航。

组件若需要私有头文件，统一放在对应组件的 `private_include/` 并通过 `PRIV_INCLUDE_DIRS` 引入；跨组件公开接口仍放在 `include/`。例如 `web_backend` 的内部 handler 声明位于 `components/app/web_backend/private_include/`，外部只应包含 `web_backend.h`。

| 模块 | 文档 |
|------|------|
| 主入口与 LP 核加载 | [main/ulp_loader/README.md](main/ulp_loader/README.md) |
| LP 核采样程序 | [main/ulp_app/README.md](main/ulp_app/README.md) |
| 全局状态 | [components/app/global_state/README.md](components/app/global_state/README.md) |
| 保护逻辑 | [components/app/protect/README.md](components/app/protect/README.md) |
| 输出控制 | [components/app/power_output/README.md](components/app/power_output/README.md) |
| 屏幕显示 | [components/app/screen/README.md](components/app/screen/README.md) |
| Shell 命令 | [components/app/shell_command/README.md](components/app/shell_command/README.md) |
| CAN 回调 | [components/app/can_callback/README.md](components/app/can_callback/README.md) |
| 电流校准 | [components/app/current_calibration/README.md](components/app/current_calibration/README.md) |
| Web 后端 | [components/app/web_backend/README.md](components/app/web_backend/README.md) |
| WiFi 服务 | [components/app/wifi_service/README.md](components/app/wifi_service/README.md) |
| 结构化黑匣子 | [components/app/blackbox_structured/README.md](components/app/blackbox_structured/README.md) |
| 硬件配置 | [components/bsp/hardware/README.md](components/bsp/hardware/README.md) |
| ST7735 驱动 | [components/bsp/st7735_driver/README.md](components/bsp/st7735_driver/README.md) |
| CAN/TWAI 驱动 | [components/bsp/HXC_TWAI/README.md](components/bsp/HXC_TWAI/README.md) |
| WiFi 管理 | [components/bsp/wifi_manager/README.md](components/bsp/wifi_manager/README.md) |
| NVS 封装 | [components/bsp/HXC_NVS/README.md](components/bsp/HXC_NVS/README.md) |
| Shell 底层 | [components/bsp/shell/README.md](components/bsp/shell/README.md) |
| ADC 采样 | [components/bsp/ADC/README.md](components/bsp/ADC/README.md) |
| PWM 输出 | [components/bsp/PWM/README.md](components/bsp/PWM/README.md) |
| 温度采样 | [components/bsp/Temperature/README.md](components/bsp/Temperature/README.md) |
| GPIO 封装 | [components/bsp/cpp_gpio_driver/README.md](components/bsp/cpp_gpio_driver/README.md) |
| Flash 循环缓冲 | [components/bsp/circular_flash_buffer/README.md](components/bsp/circular_flash_buffer/README.md) |
| 黑匣子日志引擎 | [components/middleware/blackbox/README.md](components/middleware/blackbox/README.md) |
| 按键中间件 | [components/middleware/Button/README.md](components/middleware/Button/README.md) |
| WebServer 中间件 | [components/middleware/WebServer/README.md](components/middleware/WebServer/README.md) |
| DNS Server 中间件 | [components/middleware/DNSServer/README.md](components/middleware/DNSServer/README.md) |
| 共享电量计量 | [components/middleware/energy_meter/README.md](components/middleware/energy_meter/README.md) |
| CAN 终端电阻控制 | [components/middleware/can_resistor/README.md](components/middleware/can_resistor/README.md) |
| 插值工具 | [components/common/Interp/README.md](components/common/Interp/README.md) |
| JSON 头文件库 | [components/common/json/README.md](components/common/json/README.md) |
| 字体资源 | [components/assets/Fonts/README.md](components/assets/Fonts/README.md) |
| UI 资源 | [components/assets/ui_resources/README.md](components/assets/ui_resources/README.md) |
| Web 页面资源 | [components/assets/web_file/README.md](components/assets/web_file/README.md) |

## Web 后端概览

Web 后端由 `components/app/web_backend` 提供，对外入口为 `WebBackend::start_with_wifi_service()`。组件负责注册页面路由和 REST API，底层 HTTP 能力由 `components/middleware/WebServer` 提供，网页资源由 `components/assets/web_file` 嵌入固件。

当前后端实现按职责拆分为路由注册、页面 handler、API handler、日志捕获和 JSON 请求解析等源文件。为适配 ESP32-C6 的内存约束，后端响应使用固定静态缓冲，请求 JSON 使用 common/json 的 SAX 方式按字段读取，不构造完整 JSON DOM。

详细路由表、API 示例、内存策略和源码结构见 [components/app/web_backend/README.md](components/app/web_backend/README.md)。

## 启动入口

主入口位于 `main/app_main.cpp`。当前启动流程只在主 README 保留入口级说明：

1. 初始化硬件配置、黑匣子和 NVS。
2. 初始化温度采样、状态更新定时器和屏幕任务。
3. 加载 LP 核采样程序。
4. 初始化保护、输出控制、按键、CAN、Shell。
5. 调用 `WebBackend::start_with_wifi_service()` 按配置启动 WiFi/Web。

具体模块行为以对应 README 和源码为准。

## 构建与烧录

### 环境要求

- ESP-IDF v6.0+
- 目标芯片：`esp32c6`

### 构建

```bash
idf.py set-target esp32c6
idf.py build
```

构建完成后 `scripts/post_build.py` 会自动合并固件生成 `Wireless_power_meter_lite_merged.bin`。

### 烧录

合并固件，全新烧录：

```bash
esptool.py --chip esp32c6 write_flash 0x0 Wireless_power_meter_lite_merged.bin
```

仅 APP 固件，写入 app0：

```bash
esptool.py --chip esp32c6 write_flash 0x20000 build/Wireless_power_meter_lite.bin
```

## 版本号

版本号定义为 `MAJOR.MINOR.PATCH`，在 `CMakeLists.txt` 中配置：

- `MAJOR` / `MINOR`：开发者手动修改。
- `PATCH`：`99` 表示本地构建，`0` 表示 CD 构建。

CD 通过 Tag 触发，例如 `v1.0.0`，自动注入版本号并发布 Release。

## CI/CD

- CI：推送或 PR 到 `main` 分支时自动构建。
- CD：推送 `v*` 标签时自动构建、合并固件、创建 GitHub Release 并上传固件。

## 开发环境

项目提供 VS Code Dev Container 配置，基于 ESP-IDF v6.0 Docker 镜像。

## 许可证

请参阅项目源文件头部的许可证声明。
