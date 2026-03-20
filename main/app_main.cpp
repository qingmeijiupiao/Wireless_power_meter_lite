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

#ifdef __cplusplus
#endif
#include "cpp_gpio_driver.hpp"
#include "esp_timer.h"

#include "NTCTemperatureSensor.hpp"
#include "ESPChipTemperatureSensor.hpp"

#include "ina226_interface.h"

CppGpioDriver<GPIO_NUM_16, GpioMode::OUTPUT> CAN_register;
CppGpioDriver<GPIO_NUM_21, GpioMode::OUTPUT> POWER_OUT;
CppGpioDriver<GPIO_NUM_17, GpioMode::INPUT_PULLUP> Main_Button;



ST7735::Config cfg = {
    .mosi_io_num = 4,
    .sclk_io_num = 15,
    .cs_io_num   = 0,
    .dc_io_num   = 3,
    .rst_io_num  = 2,
    .bl_io_num   = -1,   // -1 if not used
    .host_id     = SPI2_HOST
};

bool OUTPUT_state = false;
TemperatureSensor_t Chip_Temperature_Sensor;

struct main_state_t{
    float voltage;
    float current;
    bool OUTPUT_state;
    float esp_temp;
    float ntc_temp;
} main_state;


void screen_task(void* arg){
    ST7735::init(&cfg);

    uint32_t background_color=ST7735::RGB565(0, 0, 0);
    ST7735::fill_screen(background_color);
    while (1){
        //ST7735::fill_rect(-1, -2, ST7735::WIDTH, ST7735::HEIGHT, ST7735::YELLOW);
        if(OUTPUT_state){
            background_color=ST7735::RGB565(0, 255, 0);
        }else{
            background_color=ST7735::RGB565(255, 0, 0);
        }
        
        char temp_str[16];
        snprintf(temp_str, sizeof(temp_str), "C: %.2f C", main_state.esp_temp);
        ST7735::fill_screen(background_color);
        ST7735::draw_string(10, 0, temp_str,ST7735::RGB565(255, 255, 255),background_color,2);
        snprintf(temp_str, sizeof(temp_str), "N: %.2f C", main_state.ntc_temp);
        ST7735::draw_string(10, 20, temp_str,ST7735::RGB565(255, 255, 255),background_color,2);
        snprintf(temp_str, sizeof(temp_str), "V: %.2f V", main_state.voltage);
        ST7735::draw_string(10, 40, temp_str,ST7735::RGB565(255, 255, 255),background_color,2);
        snprintf(temp_str, sizeof(temp_str), "I: %.2f A", main_state.current);
        ST7735::draw_string(10, 60, temp_str,ST7735::RGB565(255, 255, 255),background_color,2);
        ST7735::sync_buffers();
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

void OUTPUT_ctrl_task(void* arg){
    bool last_button_state = Main_Button.get();
    while (1){
        if (Main_Button.get() != last_button_state){
            if(!Main_Button.get()){
                OUTPUT_state = !OUTPUT_state;
            }
            last_button_state = Main_Button.get();
        }
        POWER_OUT.set(OUTPUT_state);
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}





extern "C" void app_main(void){
    ESP_ERROR_CHECK(CAN_register.init());
    ESP_ERROR_CHECK(POWER_OUT.init());
    ESP_ERROR_CHECK(Main_Button.init());
    INA226 CurrentSensor(GPIO_NUM_6, GPIO_NUM_7,DEFAULT_INA226_I2C_ADDRESS,400000,I2C_NUM_0);
    Chip_Temperature_Sensor.init();
    NTC::init(ADC_CHANNEL_5);
    LP_Core_Load();
    BlackBox::init();

    printf("NOW LOGS COUNT: %ld\n", BlackBox::get_count());
    CAN_register.set(false);
    POWER_OUT.set(true);

    CurrentSensor.SetOperatingMode(INA226::OperatingMode::SHUNT_AND_BUS_CONTINUOUS);
    CurrentSensor.SetAveragingMode(INA226::AveragingMode::SAMPLE_16);
    CurrentSensor.SetBusVoltageConversionTime(INA226::ConversionTime::TIME_332_uS);
    CurrentSensor.SetShuntVoltageConversionTime(INA226::ConversionTime::TIME_332_uS);

    xTaskCreate(screen_task, "screen_task", 8192, NULL, 4, NULL);
    xTaskCreate(OUTPUT_ctrl_task, "OUTPUT_ctrl_task", 512, NULL, 5, NULL);

    while (1){
        // float Ctemp = Chip_Temperature_Sensor.getTemperature();
        // float Ntemp = (float)NTC::getTemperature()/100.0f;
        // int shunt = float(CurrentSensor.GetShuntVoltage_uV())/2.25;
        // int voltage = CurrentSensor.GetBusVoltage_mV();
        main_state.voltage = CurrentSensor.GetBusVoltage_mV()/1000.0f;
        main_state.current = float(CurrentSensor.GetShuntVoltage_uV())/2250.f;
        main_state.esp_temp = Chip_Temperature_Sensor.getTemperature();
        main_state.ntc_temp = (float)NTC::getTemperature()/100.0f;

        printf("Chip Temperature: %.2f C, NTC Temperature: %.2f C, Voltage: %.2f V, Current: %.2f A\n", main_state.esp_temp, main_state.ntc_temp, main_state.voltage, main_state.current);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
}