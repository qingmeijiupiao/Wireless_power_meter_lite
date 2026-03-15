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
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h" // 包含校准方案特定函数
#include "ntc_table.h"
#include "Interp.hpp"
#include "st7735.h"
#include "cpp_gpio_driver.hpp"
#include "esp_timer.h"
adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};
adc_oneshot_chan_cfg_t config = {
    .atten = ADC_ATTEN_DB_12,
    .bitwidth = ADC_BITWIDTH_DEFAULT,
};
adc_cali_handle_t cali_handle = NULL;
adc_cali_curve_fitting_config_t cali_config = {
    .unit_id = ADC_UNIT_1,
    .atten = ADC_ATTEN_DB_12,    // 必须与通道配置的衰减一致
    .bitwidth = ADC_BITWIDTH_12, // 必须与通道配置的位宽一致
};
CppGpioDriver<GPIO_NUM_16, GpioMode::OUTPUT> CAN_register;
CppGpioDriver<GPIO_NUM_21, GpioMode::OUTPUT> POWER_OUT;
int adc_value_mv = 0;


st7735_config_t cfg = {
    .mosi_io_num = 4,
    .sclk_io_num = 15,
    .cs_io_num   = 0,
    .dc_io_num   = 3,
    .rst_io_num  = 2,
    .bl_io_num   = -1,   // -1 if not used
    .host_id     = SPI2_HOST
};

void screen_task(void* arg){
    st7735_init(&cfg);
    std::vector<std::pair<int16_t, int16_t>> points;
    points.reserve(NTC_TABLE_SIZE);
    for (int i = 0; i < NTC_TABLE_SIZE; i++) {
        points.emplace_back(ntc_table[i][0], ntc_table[i][1]);
    }

    NonEquidistantInterp<int16_t, int16_t> interp_ntc(static_cast<const std::vector<std::pair<int16_t, int16_t>>&>(points));
    //st7735_fill_screen(ST7735_RGB565(0, 0, 0));
    while (1){

        int adc_value = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNEL_5, reinterpret_cast<int*>(&adc_value)));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, adc_value, &adc_value_mv));
        int16_t temp = interp_ntc.interpolate(adc_value_mv);
        char temp_str[10];
        snprintf(temp_str, sizeof(temp_str), "%d", temp);
        //st7735_fill_rect(-1, -2, ST7735_WIDTH, ST7735_HEIGHT, ST7735_YELLOW);
        st7735_fill_screen(ST7735_RGB565(0, 0, 0));
        st7735_draw_string(10, 30, temp_str,ST7735_RGB565(0xFF, 0xFF, 0xFF), ST7735_RGB565(0, 0, 0), 2);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    
}



extern "C" void app_main(void){
    ESP_ERROR_CHECK(CAN_register.init());
    ESP_ERROR_CHECK(POWER_OUT.init());
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_5, &config));
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));

    LP_Core_Load();
    BlackBox::init();

    printf("NOW LOGS COUNT: %ld\n", BlackBox::get_count());
    CAN_register.set(false);
    POWER_OUT.set(true);
    xTaskCreate(screen_task, "screen_task", 2048, NULL, 5, NULL);


    while (1){

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    // BlackBox::add_log(_log);
    // for (int i = 0; i < BlackBox::get_count(); i++) {
    //     _log=BlackBox::get_log(i);
    //     printf("LOG %d: %s\n", i, _log.strlog);
    // }
}
