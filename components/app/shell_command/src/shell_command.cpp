#include "shell_command.h"
#include "shell.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "hardware.h"
#include "st7735.h"
#include <cstdio>
#include <cstdlib>

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
            if (VERSION_PATCH == 99) {
                printf("Firmware: %d.%d.%d (Local build not official firmware!)\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
            }else{
                printf("Firmware: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
            }
            
            printf("Build:    UTC+0:%s\n", BUILD_TIME);
            printf("Hardware: v%d\n", get_hardware_version());
   
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

    // --- 添加新命令模板 ---
    // /**
    //  * @brief  <命令名> - <简要描述>
    //  * @usage  <命令名> [参数列表]
    //  * @param  <参数1> - <参数说明>
    //  * @note   <注意事项>
    //  */
    // shell.register_command(ShellCommand_t("<命令名>", "<help文本>", "<hint文本>",
    //     [](int argc, char** argv) -> int {
    //         printf("output with printf, not ESP_LOG\n");
    //         return 0;
    //     }));

    ESP_LOGI(TAG, "Shell commands registered");
    return ESP_OK;
}

}
