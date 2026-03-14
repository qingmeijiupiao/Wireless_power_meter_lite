/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_littlefs.h"
#include "blackbox.h"
#include "esp_log.h"
#include "load_lp.hpp"

#include "st7735.h"


extern "C" void app_main(void){
    LP_Core_Load();
    BlackBox::init();
    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    // for (int i = 10; i >= 0; i--) {
    //     printf("Restarting in %d seconds...\n", i);
    //     vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }
    BlackBox::BlackBoxData_t _log{
        .sof = BlackBox::DATA_SOF,
        .timestamp = esp_log_timestamp(),
        .voltage = 230.0f,
        .current = 10.0f,
        .ah = 100.0f,
        .wh = 1000.0f,
        .flags = 0,
        .strlog = "Test log",
    };
    printf("NOW LOGS COUNT: %ld\n", BlackBox::get_count());
    while (1){
        ESP_LOGI(LPTAG, "lp core COUNT: %ld", ulp_ulp_counter);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    // BlackBox::add_log(_log);
    // for (int i = 0; i < BlackBox::get_count(); i++) {
    //     _log=BlackBox::get_log(i);
    //     printf("LOG %d: %s\n", i, _log.strlog);
    // }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
