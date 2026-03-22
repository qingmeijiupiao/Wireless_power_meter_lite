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
//#include "blackbox.h"
#include "esp_log.h"
#include "load_lp.hpp"
#include "st7735.h"
#include "main_ui.h"
#ifdef __cplusplus
#endif
#include "cpp_gpio_driver.hpp"
#include "esp_timer.h"

#include "NTCTemperatureSensor.hpp"
#include "ESPChipTemperatureSensor.hpp"

#include "ina226_interface.h"
//#include "HXC_TWAI.hpp"
#include "DENGB.hpp"


CppGpioDriver<GPIO_NUM_21, GpioMode::OUTPUT> POWER_OUT;
CppGpioDriver<GPIO_NUM_17, GpioMode::INPUT_PULLUP> Main_Button;


//HXC_TWAI CAN_BUS(18,14,CAN_RATE::CAN_RATE_1MBIT);


CppGpioDriver<GPIO_NUM_16, GpioMode::OUTPUT> CAN_register;

ST7735::Config cfg = {
    .mosi_io_num = 4,
    .sclk_io_num = 15,
    .cs_io_num   = 0,
    .dc_io_num   = 3,
    .rst_io_num  = 2,
    .bl_io_num   = -1,   // -1 if not used
    .host_id     = SPI2_HOST
};

TemperatureSensor_t Chip_Temperature_Sensor;

struct main_state_t{
    float voltage;
    float current;
    bool OUTPUT_state;
    float esp_temp;
    float ntc_temp;
} main_state;


void screen_task(void* arg){
    ST7735::init(&cfg,ST7735::Rotation::Horizontal);
    ST7735::color_t background_color=ST7735::BLACK;
    ST7735::color_t text_color=ST7735::WHITE;
    ST7735::fill_screen(background_color);
    auto ticks = xTaskGetTickCount();
    constexpr int fps = 60;

    while (1){
        // if(OUTPUT_state){
        //     background_color=ST7735::BLACK;
        // }else{
        // if(OUTPUT_state){
        //     background_color=ST7735::BLACK;
        // }else{
        //     background_color=ST7735::BLACK;
        // }
        // ST7735::fill_screen(background_color);
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "C: %.2f C", main_state.esp_temp);
        ST7735::fill_screen(background_color);
        ST7735::draw_string(10, 2, temp_str,text_color,background_color,DENGB);
        snprintf(temp_str, sizeof(temp_str), "N: %.2f C", main_state.ntc_temp);
        ST7735::draw_string(10, 22, temp_str,text_color,background_color,DENGB);
        snprintf(temp_str, sizeof(temp_str), "V: %.2f V", main_state.voltage);
        ST7735::draw_string(10, 42, temp_str,text_color,background_color,DENGB);
        snprintf(temp_str, sizeof(temp_str), "I: %.2f A", std::abs(main_state.current));
        ST7735::draw_string(10, 62, temp_str,text_color,background_color,DENGB);
        if(main_state.OUTPUT_state){
            ST7735::fill_rect(100,0,160,80,ST7735::GREEN);
        }else{
            ST7735::fill_rect(100,0,160,80,ST7735::RED);
        }
        //ST7735::draw_image(0, 0, MAIN_UI_WIDTH, MAIN_UI_HEIGHT, main_ui_data);
        //ST7735::draw_string(2,2,"Hello World",text_color,background_color,DENGB);
        ST7735::sync_buffers();
        vTaskDelayUntil(&ticks, configTICK_RATE_HZ / fps);

    }
}

void OUTPUT_ctrl_task(void* arg){
    bool last_button_state = Main_Button.get();
    while (1){
        if (Main_Button.get() != last_button_state){
            if(!Main_Button.get()){
                //ESP_LOGI("OUTPUT_ctrl_task", "Button pressed");
                main_state.OUTPUT_state = !main_state.OUTPUT_state;
            }
            last_button_state = Main_Button.get();
        }
        POWER_OUT.set(main_state.OUTPUT_state);
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}





extern "C" void app_main(void){
    ESP_ERROR_CHECK(CAN_register.init());
    ESP_ERROR_CHECK(POWER_OUT.init());
    ESP_ERROR_CHECK(Main_Button.init());
    Chip_Temperature_Sensor.init();
    NTC::init(ADC_CHANNEL_5);

    INA226 CurrentSensor(GPIO_NUM_6, GPIO_NUM_7,DEFAULT_INA226_I2C_ADDRESS,400000,I2C_NUM_0);

    //LP_Core_Load();
    //BlackBox::init();

    //printf("NOW LOGS COUNT: %ld\n", BlackBox::get_count());
    
    POWER_OUT.set(true);

    CurrentSensor.SetOperatingMode(INA226::OperatingMode::SHUNT_AND_BUS_CONTINUOUS);
    CurrentSensor.SetAveragingMode(INA226::AveragingMode::SAMPLE_16);
    CurrentSensor.SetBusVoltageConversionTime(INA226::ConversionTime::TIME_332_uS);
    CurrentSensor.SetShuntVoltageConversionTime(INA226::ConversionTime::TIME_332_uS);

    xTaskCreate(screen_task, "screen_task", 4096, NULL, 4, NULL);
    xTaskCreate(OUTPUT_ctrl_task, "OUTPUT_ctrl_task", 512, NULL, 5, NULL);

    //CAN_register.set(true);



    while (1){
        main_state.voltage = CurrentSensor.GetBusVoltage_mV()/1000.0f;
        main_state.current = float(CurrentSensor.GetShuntVoltage_uV())/2250.f;
        main_state.esp_temp = Chip_Temperature_Sensor.getTemperature();
        main_state.ntc_temp = (float)NTC::getTemperature()/100.0f;

        //printf("Chip Temperature: %.2f C, NTC Temperature: %.2f C, Voltage: %.2f V, Current: %.2f A\n", main_state.esp_temp, main_state.ntc_temp, main_state.voltage, main_state.current);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
}