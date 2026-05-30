#include "shell_command.h"
#include "shell.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hardware.h"
#include "st7735.h"
#include "can_callback.h"
#include "current_calibration.h"
#include "energy_meter.h"
#include "global_state.h"
#include "HXC_NVS.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_app_desc.h"
#include "power_output.h"
#include "protect.h"
#include "wifi_service.h"
#include "web_backend.h"

namespace ShellCommand {

static const char* TAG = "ShellCommand";

// ====== 命令列表 ======

esp_err_t init() {
    auto& shell = Shell::instance();
    if(shell.init() != ESP_OK){
        return ESP_ERR_INVALID_STATE;
    }
    /**
     * @brief  reboot - 重启设备
     * @usage  reboot
     * @note   无参数，调用后设备立即重启
     */
    shell.register_command(ShellCommand_t("reboot", "Reboot the device", "",
        [](int argc, char** argv) -> int {
            printf("Rebooting...\n");
            esp_restart();
            return 0;
        }));

    /**
     * @brief  timestamp - 获取系统时间戳
     * @usage  timestamp
     * @note   输出自上电以来的微秒数，基于 esp_timer
     */
    shell.register_command(ShellCommand_t("timestamp", "Get system timestamp since boot (us)", "",
        [](int argc, char** argv) -> int {
            printf("%lld us\n", esp_timer_get_time());
            return 0;
        }));

    /**
     * @brief  version - 获取固件版本号与编译时间
     * @usage  version
     * @note   输出版本号、编译时间、硬件版本；本地固件打印 WARNING
     */
    shell.register_command(ShellCommand_t("version", "Get firmware version and build time", "",
        [](int argc, char** argv) -> int {
            printf("Hardware: v%d\n", get_hardware_version());
            if (VERSION_PATCH == 99) {
                printf("Firmware: %d.%d.%d (Local build not official firmware!)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
            }else{
                printf("Firmware: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
            }
            const esp_app_desc_t *app_desc = esp_app_get_description();
            printf("Build:    %s %s\n", app_desc->date, app_desc->time);
            return 0;
        }));

    
    /**
     * @brief  backlight - 设置屏幕背光亮度
     * @usage  backlight <0-255>
     * @param  brightness - 亮度值，0为最暗，255为最亮
     * @note   不带参数时显示当前亮度
     */
    shell.register_command(ShellCommand_t("backlight", "Set screen backlight brightness (0-255)", "<brightness>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                // 不带参数时显示当前亮度
                uint8_t current_brightness = ST7735::get_backlight();
                printf("Current backlight(0-255): %d\n", current_brightness);
                return 0;
            }
            int brightness = atoi(argv[1]);
            if (brightness < 0 || brightness > 255) {
                printf("Error: brightness must be 0-255\n");
                return 1;
            }
            esp_err_t ret = ST7735::set_backlight((uint8_t)brightness);
            if (ret != ESP_OK) {
                printf("Failed to set backlight\n");
                return 1;
            }
            printf("Backlight set to %d\n", brightness);
            return 0;
        }));

    /**
     * @brief  can_baudrate - 设置CAN波特率
     * @usage  can_baudrate [baudrate]
     * @param  baudrate - 波特率值(10进制)，如 1000000
     * @note   不带参数时显示当前波特率
     */
    shell.register_command(ShellCommand_t("can_baudrate", "Set CAN baudrate (decimal)", "<baudrate>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                printf("Current CAN baudrate: %lu\n", (uint32_t)CanCallback::CAN_BAUDRATE);
                return 0;
            }
            uint32_t baudrate = (uint32_t)strtoul(argv[1], nullptr, 10);
            if (baudrate == 0) {
                printf("Error: invalid baudrate\n");
                return 1;
            }
            CanCallback::CAN_BAUDRATE = baudrate;
            printf("CAN baudrate set to %lu\n", baudrate);
            return 0;
        }));

    /**
     * @brief  can_id - 设置CAN ID
     * @usage  can_id [id]
     * @param  id - CAN ID，支持10进制和16进制(0x前缀)，如 0x400 或 1024
     * @note   不带参数时显示当前CAN ID(同时显示10进制和16进制)
     */
    shell.register_command(ShellCommand_t("can_id", "Set CAN ID (decimal or 0x hex)", "<id>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                uint32_t id = CanCallback::CAN_ID;
                printf("Current CAN ID: %lu (0x%lX)\n", id, id);
                return 0;
            }
            uint32_t id = (uint32_t)strtoul(argv[1], nullptr, 0);
            CanCallback::CAN_ID = id;
            printf("CAN ID set to %lu (0x%lX)\n", id, id);
            return 0;
        }));
    
    /**
     * @brief  get_data - 获取当前数据
     * @usage  get_data
     * @note   显示当前电压V,电流A,温度°C
     */
    shell.register_command(ShellCommand_t("get_data", "Get current data (voltage, current, temperature)", "",
        [](int argc, char** argv) -> int {
            auto state = get_global_state();
            float voltage = (float)state.voltage_mV/1e3;
            float current = (float)state.current_uA/1e6;
            float temperature = (float)state.board_temperature/100.0;
            printf("voltage: %.3f V, current: %.3f A, temperature: %.2f C\n", voltage, current, temperature);
            return 0;
        }));

    shell.register_command(ShellCommand_t("meter", "Get or reset shared energy meter session", "[status|reset]",
        [](int argc, char** argv) -> int {
            if (argc >= 2 && strcmp(argv[1], "reset") == 0) {
                EnergyMeter::reset();
                printf("Shared meter session reset\n");
            } else if (argc >= 2 && strcmp(argv[1], "status") != 0) {
                printf("Usage: meter [status|reset]\n");
                return 1;
            }

            const auto meter = EnergyMeter::snapshot();
            const auto& state = get_global_state();
            const uint64_t meter_seconds = meter.meter_time_ms / 1000;
            const uint64_t system_time_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
            const uint64_t system_seconds = system_time_ms / 1000;
            const double voltage_v = state.voltage_mV / 1000.0;
            const double current_a = state.current_uA / 1000000.0;

            printf("Shared meter session:\n");
            printf("  energy:       %.3f mWh (%lld uWh)\n", meter.energy_uwh / 1000.0, static_cast<long long>(meter.energy_uwh));
            printf("  charge:       %.3f mAh (%lld uAh)\n", meter.charge_uah / 1000.0, static_cast<long long>(meter.charge_uah));
            printf("  meter time:   %llu ms (%02llu:%02llu:%02llu)\n",
                   static_cast<unsigned long long>(meter.meter_time_ms),
                   static_cast<unsigned long long>(meter_seconds / 3600),
                   static_cast<unsigned long long>((meter_seconds / 60) % 60),
                   static_cast<unsigned long long>(meter_seconds % 60));
            printf("LP Core lifetime counters:\n");
            printf("  energy:       %ld uWh\n", static_cast<long>(state.meter_uwh));
            printf("  charge:       %ld uAh\n", static_cast<long>(state.meter_uah));
            printf("System:\n");
            printf("  uptime:       %llu ms (%02llu:%02llu:%02llu)\n",
                   static_cast<unsigned long long>(system_time_ms),
                   static_cast<unsigned long long>(system_seconds / 3600),
                   static_cast<unsigned long long>((system_seconds / 60) % 60),
                   static_cast<unsigned long long>(system_seconds % 60));
            printf("  voltage:      %.3f V\n", voltage_v);
            printf("  current:      %.6f A\n", current_a);
            printf("  power:        %.3f W\n", voltage_v * current_a);
            return 0;
        }));
        
    /**
     * @brief  output - 输出控制
     * @param  state - 输出状态，0为关闭，1为开启
     * @usage  output <state>
     * @note   设置输出状态，0为关闭，1为开启
     */
    shell.register_command(ShellCommand_t("output", "Set output state (0-1)", "<state>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                printf("Now output state: %d\n", PowerOutput::get_state());
                return 1;
            }
            int state = atoi(argv[1]);
            if (state < 0 || state > 1) {
                printf("Error: state must be 0-1\n");
                return 1;
            }
            if(state == 0){
                PowerOutput::off();
                printf("Output off\n");
            }else{
                PowerOutput::on();
                printf("Output on\n");
            }
            return 0;
        }));

    /**
     * @brief  protect - 保护功能开关
     * @param  state - 保护状态，0为关闭保护，1为开启保护
     * @usage  protect [state]
     * @note   不带参数时显示当前保护开关状态和保护触发状态
     */
    shell.register_command(ShellCommand_t("protect", "Set protect state (0-1)", "<state>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                auto state_to_str = [](ProtectState_t state) -> const char* {
                    switch(state){
                        case PROTECT_STATE_NORMAL: return "NORMAL";
                        case PROTECT_STATE_WARNING: return "WARNING";
                        case PROTECT_STATE_PROTECT: return "PROTECT";
                        default: return "UNKNOWN";
                    }
                };
                printf("Protect state: %d, bypassed: %d, active fault: %d\n", !protect_is_bypassed(), protect_is_bypassed(), protect_has_active_fault());
                printf("Name State   Now       Warn/Recover      Protect/Recover    Trigger\n");
                for(uint8_t i = 0; i < protect_get_channel_count(); i++){
                    protect_channel_info_t info;
                    if(!protect_get_channel_info(i, &info)){
                        continue;
                    }
                    printf("%-4s %-7s %7.3f%-2s %7.3f/%-7.3f %7.3f/%-7.3f %s\n",
                        info.name,
                        state_to_str(info.state),
                        info.now_value,
                        info.unit,
                        info.threshold.warning_threshold,
                        info.threshold.warning_recovery_threshold,
                        info.threshold.protect_threshold,
                        info.threshold.protect_recovery_threshold,
                        info.threshold.is_asc ? ">=" : "<=");
                }
                return 0;
            }
            int state = atoi(argv[1]);
            if (state < 0 || state > 1) {
                printf("Error: state must be 0-1\n");
                return 1;
            }
            protect_set_bypassed(state == 0);
            printf("Protect %s\n", state ? "on" : "off");
            if (state == 1 && protect_should_block_output()) {
                PowerOutput::off();
                printf("Active protect fault exists, output forced off\n");
            }
            return 0;
        }));

    /**
     * @brief  wifi - WiFi/Web 功能管理
     * @usage  wifi [status|ip|on|off|connect|ap|boot|clear]
     * @param  status/ip - 查询当前 WiFiService 模式、IP、已保存 SSID、配网 AP 名称和最近错误
     * @param  on - 按 NVS 配置启动 WiFi/Web，优先连接已保存 STA，失败则进入 AP 配网模式
     * @param  off - 停止 WiFiService 管理的网络功能
     * @param  connect <ssid> [password] - 连接指定 WiFi，成功后保存到 NVS
     * @param  ap - 手动切换到 AP 配网模式
     * @param  boot <0|1> - 设置启动时是否默认启用 WiFi/Web 功能
     * @param  clear - 清除已保存的 STA SSID 和密码
     * @note   命令通过 WifiService 统一处理状态机，不直接调用底层 esp_wifi；Web 启动失败时返回错误码，不触发重启。
     */
    shell.register_command(ShellCommand_t("wifi", "WiFi/Web control", "status|ip|on|off|connect|ap|boot|clear",
        [](int argc, char** argv) -> int {
            auto mode_to_str = [](WifiService::Mode mode) -> const char* {
                switch(mode){
                    case WifiService::Mode::OFF: return "OFF";
                    case WifiService::Mode::STA: return "STA";
                    case WifiService::Mode::AP_PROVISION: return "AP_PROVISION";
                    default: return "UNKNOWN";
                }
            };

            auto print_status = [&]() {
                IP_t ip = WifiService::get_ip();
                MAC_t sta_mac = WiFiManager::instance().get_mac(WIFI_IF_STA);
                MAC_t ap_mac = WiFiManager::instance().get_mac(WIFI_IF_AP);
                auto cfg = WifiService::get_config();
                printf("mode: %s, wifi_state: %d, web_running: %d, boot_enabled: %d\n",
                       mode_to_str(WifiService::get_mode()),
                       (int)WifiService::get_wifi_state(),
                       WebBackend::is_running(),
                       cfg.web_enabled_on_boot);
                printf("ip: %u.%u.%u.%u, saved_ssid: %s, ap_ssid: %s, last_error: %s\n",
                       ip.octet1, ip.octet2, ip.octet3, ip.octet4,
                       cfg.ssid[0] ? cfg.ssid : "(none)",
                       WifiService::get_ap_ssid(),
                       WifiService::get_last_error());
                printf("sta_mac: %02X:%02X:%02X:%02X:%02X:%02X, ap_mac: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       sta_mac.octet1, sta_mac.octet2, sta_mac.octet3,
                       sta_mac.octet4, sta_mac.octet5, sta_mac.octet6,
                       ap_mac.octet1, ap_mac.octet2, ap_mac.octet3,
                       ap_mac.octet4, ap_mac.octet5, ap_mac.octet6);
            };

            if(argc < 2 || strcmp(argv[1], "status") == 0 || strcmp(argv[1], "ip") == 0){
                print_status();
                return 0;
            }

            if(strcmp(argv[1], "on") == 0){
                esp_err_t web_init_ret = WebBackend::init();
                esp_err_t web_start_ret = WebBackend::start();
                if(web_init_ret != ESP_OK || web_start_ret != ESP_OK){
                    printf("web start failed: %s %s\n", esp_err_to_name(web_init_ret), esp_err_to_name(web_start_ret));
                    return 1;
                }
                esp_err_t ret = WifiService::start_default();
                printf("wifi on: %s\n", esp_err_to_name(ret));
                print_status();
                return ret == ESP_OK ? 0 : 1;
            }

            if(strcmp(argv[1], "off") == 0){
                esp_err_t ret = WifiService::stop();
                printf("wifi off: %s\n", esp_err_to_name(ret));
                return ret == ESP_OK ? 0 : 1;
            }

            if(strcmp(argv[1], "connect") == 0){
                if(argc < 3){
                    printf("Usage: wifi connect <ssid> [password]\n");
                    return 1;
                }
                const char* password = argc >= 4 ? argv[3] : "";
                esp_err_t web_init_ret = WebBackend::init();
                esp_err_t web_start_ret = WebBackend::start();
                if(web_init_ret != ESP_OK || web_start_ret != ESP_OK){
                    printf("web start failed: %s %s\n", esp_err_to_name(web_init_ret), esp_err_to_name(web_start_ret));
                    return 1;
                }
                esp_err_t ret = WifiService::connect_sta(argv[2], password, true);
                printf("wifi connect: %s\n", esp_err_to_name(ret));
                print_status();
                return ret == ESP_OK ? 0 : 1;
            }

            if(strcmp(argv[1], "ap") == 0){
                esp_err_t web_init_ret = WebBackend::init();
                esp_err_t web_start_ret = WebBackend::start();
                if(web_init_ret != ESP_OK || web_start_ret != ESP_OK){
                    printf("web start failed: %s %s\n", esp_err_to_name(web_init_ret), esp_err_to_name(web_start_ret));
                    return 1;
                }
                esp_err_t ret = WifiService::start_provision_ap();
                printf("wifi ap: %s\n", esp_err_to_name(ret));
                print_status();
                return ret == ESP_OK ? 0 : 1;
            }

            if(strcmp(argv[1], "boot") == 0){
                if(argc < 3){
                    printf("boot_enabled: %d\n", WifiService::is_web_enabled_on_boot());
                    return 0;
                }
                int enabled = atoi(argv[2]);
                if(enabled < 0 || enabled > 1){
                    printf("Usage: wifi boot <0|1>\n");
                    return 1;
                }
                WifiService::set_web_enabled_on_boot(enabled == 1);
                printf("boot_enabled set to %d\n", enabled);
                return 0;
            }

            if(strcmp(argv[1], "clear") == 0){
                WifiService::clear_saved_sta();
                printf("saved wifi cleared\n");
                return 0;
            }

            printf("Usage: wifi status|ip|on|off|connect <ssid> [password]|ap|boot <0|1>|clear\n");
            return 1;
        }));


    /**
     * @brief  ina226_register - 获取ina226寄存器值
     * @usage  ina226_register <register_addr>
     * @note   显示当前ina226电压电流寄存器值
     */
    shell.register_command(ShellCommand_t("ina226_register", "Get ina226 register value", "",
        [](int argc, char** argv) -> int {
            printf("ina226_register_raw current: %d, voltage: %d\n", *current_register_raw, *voltage_register_raw);
            return 0;
        }));

    /**
     * @brief  calibration_params - 获取校准参数
     * @usage  calibration_params
     * @note   显示当前校准参数
     */
    shell.register_command(ShellCommand_t("calibration_params", "Get calibration params", "",
        [](int argc, char** argv) -> int {
            auto params = CurrentCalib::params_data.read();
            printf("Current calibration params:\n");
            printf("Calibration current basek: %d == sample resistance_mOhm: %.3f\n", params.current_base_K,float(2500.f/params.current_base_K));//电流参数K与采样电阻值的计算推导见current_calibration的README
            printf("Calibration current current points:\n");
            for(int i = 0; i < sizeof(params.points)/sizeof(params.points[0]); i++){
                printf("Calibration index %d: reg_raw_value %d,no_offset_mA %d, cali_offset_uA %d\n", i, params.points[i].register_value,params.points[i].register_value*params.current_base_K/1000, params.points[i].offset_current_100uA*100);
            }
            printf("Calibration current temperatureK: %d\n", params.temperature_K);
            return 0;
        }));

    /*============工厂模式命令==================*/
    // 校准基准电流K值
    static ShellCommand_t calibration_basek("calibration_basek", "Calibration current basek Value","<basek>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                printf("Error: basek must be specified\n");
                return 1;
            }
            auto params = CurrentCalib::params_data.read();
            int basek = atoi(argv[1]);
            params.current_base_K = basek;
            CurrentCalib::params_data = params;
            printf("Calibration basek set to %d restart required\n", basek);
            return 0;
        });

    // 校准温度K值
    static ShellCommand_t calibration_current_temperatureK("calibration_current_temperatureK", "Calibration current current temperatureK Value","<temperatureK>",
        [](int argc, char** argv) -> int {
            if (argc < 2) {
                printf("Error: temperatureK must be specified\n");
                return 1;
            }
            auto params = CurrentCalib::params_data.read();
            int temperatureK = atoi(argv[1]);
            params.temperature_K = temperatureK;
            CurrentCalib::params_data = params;
            printf("Calibration temperatureK set to %d restart required\n", temperatureK);
            return 0;
        });
    // 校准电流校点
    static ShellCommand_t calibration_current_points("calibration_current_points", "Calibration current current points Value","<point_index> <register_value> <offset_current_uA>",
        [](int argc, char** argv) -> int {
            if (argc < 4) {
                printf("Error: point_index, register_value, voltage_uv must be specified\n");
                return 1;
            }
            auto params = CurrentCalib::params_data.read();
            int point_index = atoi(argv[1]);
            if(point_index < 0 || point_index >= sizeof(params.points)/sizeof(params.points[0])) {
                printf("Error: point_index out of range\n");
                return 1;
            }

            int register_value = std::abs(atoi(argv[2]));
            int new_offset_current_100uA = atoi(argv[3])/100;
            params.points[point_index].register_value = register_value;
            params.points[point_index].offset_current_100uA = new_offset_current_100uA;
            CurrentCalib::params_data = params;
            printf("Calibration current points set to %d, %d(uA) restart required\n", register_value, atoi(argv[3]));
            return 0;
        });

    static ShellCommand_t calibration_clear("calibration_clear", "Clear calibration params (keep base_K)", "",
        [](int argc, char** argv) -> int {
            auto params = CurrentCalib::params_data.read();
            uint16_t base_k = params.current_base_K;
            memset(params.points, 0, sizeof(params.points));
            params.temperature_K = 0;
            params.current_base_K = base_k;
            CurrentCalib::params_data = params;
            printf("Calibration params cleared (base_K=%d preserved)\n", base_k);
            return 0;
        });

    shell.register_command(ShellCommand_t("factory_mode", "Enter factory mode", "",
        [](int argc, char** argv) -> int {
            auto& _shell = Shell::instance();
            protect_set_bypassed(true);
            _shell.register_command(calibration_basek);
            _shell.register_command(calibration_current_temperatureK);
            _shell.register_command(calibration_current_points);
            _shell.register_command(calibration_clear);
            printf("Factory mode enabled, protect bypass enabled\n");
            return 0;
        }));

    ESP_LOGI(TAG, "Shell commands registered");
    return ESP_OK;
}

}
