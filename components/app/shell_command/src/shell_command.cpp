#include "shell_command.h"
#include "shell.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hardware.h"
#include "st7735.h"
#include "can_callback.h"
#include "current_calibration.h"
#include "global_state.h"
#include "HXC_NVS.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_app_desc.h"
#include "power_output.h"

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
            printf("Calibration current basek: %d\n", params.current_base_K);
            printf("Calibration current current points:\n");
            for(int i = 0; i < sizeof(params.points)/sizeof(params.points[0]); i++){
                printf("Calibration index %d: reg_raw_value %d,no_offset_mA %d, cali_offset_uA %d\n", i, params.points[i].register_value,params.points[i].register_value*params.current_base_K/1000, params.points[i].offset_current_uA);
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

            int register_value = atoi(argv[2]);
            int new_offset_current_uA = atoi(argv[3]);
            params.points[point_index].register_value = register_value;
            params.points[point_index].offset_current_uA = new_offset_current_uA;
            CurrentCalib::params_data = params;
            printf("Calibration current points set to %d, %d restart required\n", register_value, new_offset_current_uA);
            return 0;
        });

    shell.register_command(ShellCommand_t("factory_mode", "Enter factory mode", "",
        [](int argc, char** argv) -> int {
            auto& _shell = Shell::instance();
            _shell.register_command(calibration_basek);
            _shell.register_command(calibration_current_temperatureK);
            _shell.register_command(calibration_current_points);
            printf("Factory mode enabled\n");
            return 0;
        }));

    ESP_LOGI(TAG, "Shell commands registered");
    return ESP_OK;
}

}
