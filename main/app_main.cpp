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

#include "ui_static.h"
#include "ui_open.h"
#include "ui_close.h"

#include "cpp_gpio_driver.hpp"
#include "esp_timer.h"

#include "NTCTemperatureSensor.hpp"
#include "ESPChipTemperatureSensor.hpp"
#include "HXC_TWAI.h"

#include "global_state.h"

#include "DENGB20.h"
#include "DENGB16.h"
#include "DENGB12.h"
#include "ErrorRectangle.h"
#include "WarningRectangle.h"

CppGpioDriver<GPIO_NUM_21, GpioMode::OUTPUT> POWER_OUT;
CppGpioDriver<GPIO_NUM_17, GpioMode::INPUT_PULLUP> Main_Button;
CppGpioDriver<GPIO_NUM_16, GpioMode::OUTPUT> CAN_register;

GlobalState& global_state = get_global_state();

HXC_TWAI CAN_BUS(18,14,CAN_RATE::CAN_RATE_1MBIT);

TemperatureSensor_t Chip_Temperature_Sensor;

ST7735::Config cfg = {
    .mosi_io_num = 4,
    .sclk_io_num = 15,
    .cs_io_num   = 0,
    .dc_io_num   = 3,
    .rst_io_num  = 2,
    .bl_io_num   = -1,   // -1 if not used
    .host_id     = SPI2_HOST
};

class now_time_t{
    public:
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    void update(uint32_t ms){
        uint32_t total_seconds = ms / 1000;
        second = total_seconds % 60;
        uint32_t total_minutes = total_seconds / 60;
        minute = total_minutes % 60;
        hour = total_minutes / 60;
    };
};

void screen_task(void* arg){
    
    ST7735::init(&cfg,ST7735::Rotation::Horizontal);
    ST7735::color_t background_color=ST7735::BLACK;
    ST7735::fill_screen(background_color);
    auto ticks = xTaskGetTickCount();
    constexpr int fps = 60;
    ST7735::draw_image(4, 4, STATIC_WIDTH, STATIC_HEIGHT, static_data);
    ST7735::fill_rect(106, 0, 2, 80, ST7735::YELLOW);
    ST7735::fill_rect(108, 13, 52, 2, ST7735::YELLOW);
    ST7735::sync_buffers();
    ST7735::draw_image(4, 4, STATIC_WIDTH, STATIC_HEIGHT, static_data);
    ST7735::fill_rect(106, 0, 2, 80, ST7735::YELLOW);
    ST7735::fill_rect(108, 13, 52, 2, ST7735::YELLOW);
    now_time_t now;
    now.update(ticks);

    ST7735::color_t warning_background_color;
    warning_background_color.set_color_raw(0xFE60);
    ST7735::color_t error_background_color;
    error_background_color.set_color_raw(0xB123);

    while (1){

        char temp_str[16];
        // snprintf(temp_str, sizeof(temp_str), "C: %.2f C", global_state.chip_temperature);
        // ST7735::fill_screen(background_color);
        // ST7735::draw_string(10, 2, temp_str,text_color,background_color,DENGB);
        // snprintf(temp_str, sizeof(temp_str), "N: %.2f C", global_state.NTC_temperature);
        // ST7735::draw_string(10, 22, temp_str,text_color,background_color,DENGB);
        ST7735::fill_rect(28, 2, 106-29, 61, ST7735::BLACK); // 清除W文字显示区域防止位数变化导致的显示错误

        now.update(ticks);
        float voltage = ulp_voltage_uv/1e6;
        float current = std::abs(*(int32_t*)&ulp_current_nA/1e6);
        snprintf(temp_str, sizeof(temp_str), "%.3fV", voltage);
        ST7735::draw_string(28, 2, temp_str,ST7735::color_t(0xef2a2a),background_color,DENGB20);
        snprintf(temp_str, sizeof(temp_str), "%.3fA", current);
        ST7735::draw_string(28, 25, temp_str,ST7735::color_t(0x1ef851),background_color,DENGB20);
        
        snprintf(temp_str, sizeof(temp_str), "%.3fW", current*voltage);
        ST7735::draw_string(28, 47, temp_str,ST7735::color_t(0x003ED0),background_color,DENGB16);

        snprintf(temp_str, sizeof(temp_str), "%.1fC", global_state.NTC_temperature);
        ST7735::draw_string(28, 67, temp_str,ST7735::color_t(0xb3261e),background_color,DENGB12);

        snprintf(temp_str, sizeof(temp_str), "%02d:%02d:%02d", now.hour, now.minute, now.second);
        ST7735::draw_string(111, 0, temp_str,ST7735::color_t(0xffffff),background_color,DENGB12);

        if(global_state.out_put_state){
            ST7735::draw_image(65, 66, OPEN_WIDTH, OPEN_HEIGHT, open_data);
        }else{
            ST7735::draw_image(65, 66, CLOSE_WIDTH, CLOSE_HEIGHT, close_data);
        }

        ProtectState_t temp_protect_state = global_state.protect_states.temperature_protect_state;
        if(temp_protect_state != PROTECT_STATE_NORMAL){
            if(temp_protect_state == PROTECT_STATE_PROTECT){
                ST7735::draw_image(113,18,ERRORRECTANGLE_WIDTH,ERRORRECTANGLE_HEIGHT,ErrorRectangle_data);
                ST7735::draw_string(113+5, 18, "OTP",ST7735::color_t(0x000000),error_background_color,DENGB16);
            }else if(temp_protect_state == PROTECT_STATE_WARNING){
                ST7735::draw_image(113,18,WARNINGRECTANGLE_WIDTH,WARNINGRECTANGLE_HEIGHT,WarningRectangle_data);
                ST7735::draw_string(113+5, 18, "OTP",ST7735::color_t(0x000000),warning_background_color,DENGB16);
            }

        }else{
            ST7735::fill_rect(113, 18, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, background_color);
        }
        
        ProtectState_t voltage_protect_state = global_state.protect_states.voltage_protect_state;
        if(voltage_protect_state != PROTECT_STATE_NORMAL){
            if(voltage_protect_state == PROTECT_STATE_PROTECT){
                ST7735::draw_image(113,18+21,ERRORRECTANGLE_WIDTH,ERRORRECTANGLE_HEIGHT,ErrorRectangle_data);
                ST7735::draw_string(113+5, 18+21, "OVP",ST7735::color_t(0x000000),error_background_color,DENGB16);
            }else if(voltage_protect_state == PROTECT_STATE_WARNING){
                ST7735::draw_image(113,18+21,WARNINGRECTANGLE_WIDTH,WARNINGRECTANGLE_HEIGHT,WarningRectangle_data);
                ST7735::draw_string(113+5, 18+21, "OVP",ST7735::color_t(0x000000),warning_background_color,DENGB16);
            }

        }else{
            ST7735::fill_rect(113, 18+21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, background_color);
        }
        
        ProtectState_t current_protect_state = global_state.protect_states.current_protect_state;
        if(current_protect_state != PROTECT_STATE_NORMAL){
            if(current_protect_state == PROTECT_STATE_PROTECT){
                ST7735::draw_image(113,18+21+21,ERRORRECTANGLE_WIDTH,ERRORRECTANGLE_HEIGHT,ErrorRectangle_data);
                ST7735::draw_string(113+5, 18+21+21, "OCP",ST7735::color_t(0x000000),error_background_color,DENGB16);
            }else if(current_protect_state == PROTECT_STATE_WARNING){
                ST7735::draw_image(113,18+21+21,WARNINGRECTANGLE_WIDTH,WARNINGRECTANGLE_HEIGHT,WarningRectangle_data);
                ST7735::draw_string(113+5, 18+21+21, "OCP",ST7735::color_t(0x000000),warning_background_color,DENGB16);
            }

        }else{
            ST7735::fill_rect(113, 18+21+21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, background_color);
        }

        //ESP_LOGI("screen_task", "voltage=%.3fV, current=%.3fA", voltage, current);
        ST7735::sync_buffers();
        vTaskDelayUntil(&ticks, configTICK_RATE_HZ / fps);

    }
}

void update_main_state_task(void* arg){
    auto ticks = xTaskGetTickCount();
    constexpr int update_HZ = 200;
    while (1){
        global_state.voltage_uV = ulp_voltage_uv;
        global_state.current_nA = ulp_current_nA;
        global_state.NTC_temperature = NTC::getTemperature()/100.0f;
        global_state.chip_temperature = Chip_Temperature_Sensor.getTemperature();
        xTaskDelayUntil(&ticks, configTICK_RATE_HZ / update_HZ);
    }
}


void OUTPUT_ctrl_task(void* arg){
    bool last_button_state = Main_Button.get();
    bool now_button_state = Main_Button.get();
    const uint32_t button_check_HZ = 100;
    auto ticks = xTaskGetTickCount();
    while (1){
        now_button_state = Main_Button.get();
        if (now_button_state != last_button_state){
            if(!now_button_state){
                ESP_LOGI("OUTPUT_ctrl_task", "Button pressed");
                global_state.out_put_state = !global_state.out_put_state;
            }
            last_button_state = now_button_state;
        }

        // 保护状态判断, 有保护状态时, 输出关闭
        if(global_state.protect_states.voltage_protect_state == PROTECT_STATE_PROTECT || 
           global_state.protect_states.current_protect_state == PROTECT_STATE_PROTECT || 
           global_state.protect_states.temperature_protect_state == PROTECT_STATE_PROTECT){
            global_state.out_put_state = false;
        }

        POWER_OUT.set(global_state.out_put_state);
        xTaskDelayUntil(&ticks, configTICK_RATE_HZ / button_check_HZ);
    }
}

void test_callback(HXC_CAN_message_t* can_message){
    ESP_LOGI("HXC_TWAI", "ID=%08lX", can_message->identifier);
}

const uint8_t send_data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
const twai_frame_t send_message = {
    .header = {
        .id = 0x123,
        .dlc = 8,
    },
    .buffer = const_cast<uint8_t*>(send_data),
    .buffer_len = sizeof(send_data),
};


void CAN_test_task(void* arg){
    CAN_register.set(true);
    ESP_ERROR_CHECK(CAN_BUS.setup());

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    CAN_BUS.add_can_receive_callback_func(0x123,test_callback);


    // send_message.data_length_code = 8;
    // send_message.identifier = 0x123;
    // send_message.extd = false;
    // send_message.rtr = false;
    // send_message.data[0] = 0x01;
    // send_message.data[1] = 0x02;
    // send_message.data[2] = 0x03;
    // send_message.data[3] = 0x04;
    // send_message.data[4] = 0x05;
    // send_message.data[5] = 0x06;
    // send_message.data[6] = 0x07;
    // send_message.data[7] = 0x08;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    uint8_t send_count = 0;
    while (1){
        ESP_LOGI("HXC_TWAI", "test_count: %d", test_count);
        auto ret = twai_node_transmit(CAN_BUS.twai_node_handle, &send_message, 0);
        if(ret != ESP_OK){
            ESP_LOGE("HXC_TWAI", "twai_node_transmit failed %s", esp_err_to_name(ret));
        }else{
            send_count++;
            ESP_LOGI("HXC_TWAI", "send_count: %d", send_count);
        }
        //ESP_ERROR_CHECK(CAN_BUS.send(&send_message));
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


extern "C" void app_main(void){
    ESP_ERROR_CHECK(CAN_register.init());
    ESP_ERROR_CHECK(POWER_OUT.init());
    ESP_ERROR_CHECK(Main_Button.init());
    ESP_ERROR_CHECK(Chip_Temperature_Sensor.init());
    ESP_ERROR_CHECK(NTC::init(ADC_CHANNEL_5));
    ESP_ERROR_CHECK(LP_Core_Load());
    ESP_ERROR_CHECK(BlackBox::init());

    //printf("NOW LOGS COUNT: %ld\n", BlackBox::get_count());
    
    POWER_OUT.set(true);
    xTaskCreate(update_main_state_task, "update_main_state_task", 2048, NULL, 6, NULL);
    xTaskCreate(screen_task, "screen_task", 4096, NULL, 4, NULL);
    xTaskCreate(OUTPUT_ctrl_task, "OUTPUT_ctrl_task", 2048, NULL, 5, NULL);
    xTaskCreate(protect_task, "protect_task", 2048, NULL, 5, NULL);

    //xTaskCreate(CAN_test_task, "CAN_test_task", 8192, NULL, 6, NULL);

    while (1){
        //ESP_LOGI("app_main", "protect_states: temp=%d, voltage=%d, current=%d", global_state.protect_states.temperature_protect_state, global_state.protect_states.voltage_protect_state, global_state.protect_states.current_protect_state);
        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
    
}