#include "screen.h"
#include "st7735.h"
#include "hardware.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "protect.h"

#include "ui_static.h"
#include "ui_open.h"
#include "ui_close.h"
#include "DENGB20.h"
#include "DENGB16.h"
#include "DENGB12.h"
#include "ErrorRectangle.h"
#include "WarningRectangle.h"

namespace SCREEN {

class now_time_t {
public:
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    void update(uint32_t ms) {
        uint32_t total_seconds = ms / 1000;
        second = total_seconds % 60;
        uint32_t total_minutes = total_seconds / 60;
        minute = total_minutes % 60;
        hour = total_minutes / 60;
    };
};

void screen_task(void* arg) {
    auto hardware_config = get_hardware_config();
    static ST7735::Config cfg = {};
    cfg.sclk_io_num = hardware_config.TFT_SCL;
    cfg.mosi_io_num = hardware_config.TFT_SDA;
    cfg.cs_io_num   = hardware_config.TFT_CS;
    cfg.dc_io_num   = hardware_config.TFT_RS;
    cfg.rst_io_num  = hardware_config.TFT_RST;
    cfg.bl_io_num   = hardware_config.TFT_BLK;
    cfg.bl_active_state = hardware_config.TFT_BLK_ACTIVE_STATE;
    cfg.host_id     = SPI2_HOST;
    esp_err_t ret = ST7735::init(&cfg, ST7735::Rotation::Horizontal);
    if (ret != ESP_OK) {
        ESP_LOGE("screen_task", "ST7735 init failed");
    }
    ST7735::color_t background_color = ST7735::BLACK;
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
    
    ST7735::color_t warning_background_color;
    warning_background_color.set_color_raw(0xFE60);
    ST7735::color_t error_background_color;
    error_background_color.set_color_raw(0xB123);
    ST7735::set_backlight(30 * 255 / 100);
    while (1) {
        now.update(ticks);
        char temp_str[16];
        auto& global_state = get_global_state();
        auto& global_state_bits = global_state.global_state_bits;
        auto& protect_states = global_state.protect_states.states_bit;

        ST7735::fill_rect(28, 2, 106 - 29, 61, ST7735::BLACK);

        float voltage = global_state.voltage_mV / 1000.0f;
        float current = std::abs(global_state.current_uA / 1e6);
        snprintf(temp_str, sizeof(temp_str), "%.3fV", voltage);
        ST7735::draw_string(28, 2, temp_str, ST7735::color_t(0xef2a2a), background_color, DENGB20);
        snprintf(temp_str, sizeof(temp_str), "%.3fA", current);
        ST7735::draw_string(28, 25, temp_str, ST7735::color_t(0x1ef851), background_color, DENGB20);

        snprintf(temp_str, sizeof(temp_str), "%.3fW", current * voltage);
        ST7735::draw_string(28, 47, temp_str, ST7735::color_t(0x003ED0), background_color, DENGB16);

        float temperature = global_state.board_temperature / 100.0f;
        if(temperature >= 100.0f || temperature < 0.00f){
            snprintf(temp_str, sizeof(temp_str), "%dC", (int)temperature);
        }else{
            snprintf(temp_str, sizeof(temp_str), "%.1fC", temperature);
        }
        ST7735::draw_string(28, 67, temp_str, ST7735::color_t(0xb3261e), background_color, DENGB12);

        snprintf(temp_str, sizeof(temp_str), "%02d:%02d:%02d", now.hour, now.minute, now.second);
        ST7735::draw_string(111, 0, temp_str, ST7735::color_t(0xffffff), background_color, DENGB12);

        if (global_state_bits.state_bit.out_put_state) {
            ST7735::draw_image(62, 66, OPEN_WIDTH, OPEN_HEIGHT, open_data);
        } else {
            ST7735::draw_image(62, 66, CLOSE_WIDTH, CLOSE_HEIGHT, close_data);
        }

        ProtectState_t temp_protect_state = protect_states.temperature_protect_state;
        if (temp_protect_state != PROTECT_STATE_NORMAL) {
            if (temp_protect_state == PROTECT_STATE_PROTECT) {
                ST7735::draw_image(113, 18, ERRORRECTANGLE_WIDTH, ERRORRECTANGLE_HEIGHT, ErrorRectangle_data);
                ST7735::draw_string(113 + 5, 18, "OTP", ST7735::color_t(0x000000), error_background_color, DENGB16);
            } else if (temp_protect_state == PROTECT_STATE_WARNING) {
                ST7735::draw_image(113, 18, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, WarningRectangle_data);
                ST7735::draw_string(113 + 5, 18, "OTP", ST7735::color_t(0x000000), warning_background_color, DENGB16);
            }
        } else {
            ST7735::fill_rect(113, 18, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, background_color);
        }

        ProtectState_t high_voltage_protect_state = protect_states.high_voltage_protect_state;
        ProtectState_t low_voltage_protect_state = protect_states.low_voltage_protect_state;

        if (high_voltage_protect_state != PROTECT_STATE_NORMAL) {
            if (high_voltage_protect_state == PROTECT_STATE_PROTECT) {
                ST7735::draw_image(113, 18 + 21, ERRORRECTANGLE_WIDTH, ERRORRECTANGLE_HEIGHT, ErrorRectangle_data);
                ST7735::draw_string(113 + 5, 18 + 21, "OVP", ST7735::color_t(0x000000), error_background_color, DENGB16);
            } else if (high_voltage_protect_state == PROTECT_STATE_WARNING) {
                ST7735::draw_image(113, 18 + 21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, WarningRectangle_data);
                ST7735::draw_string(113 + 5, 18 + 21, "OVP", ST7735::color_t(0x000000), warning_background_color, DENGB16);
            }
        } else if (low_voltage_protect_state != PROTECT_STATE_NORMAL) {
            if (low_voltage_protect_state == PROTECT_STATE_PROTECT) {
                ST7735::draw_image(113, 18 + 21, ERRORRECTANGLE_WIDTH, ERRORRECTANGLE_HEIGHT, ErrorRectangle_data);
                ST7735::draw_string(113 + 5, 18 + 21, "UVP", ST7735::color_t(0x000000), error_background_color, DENGB16);
            } else if (low_voltage_protect_state == PROTECT_STATE_WARNING) {
                ST7735::draw_image(113, 18 + 21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, WarningRectangle_data);
                ST7735::draw_string(113 + 5, 18 + 21, "UVP", ST7735::color_t(0x000000), warning_background_color, DENGB16);
            }
        } else {
            ST7735::fill_rect(113, 18 + 21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, background_color);
        }

        ProtectState_t current_protect_state = protect_states.current_protect_state;
        if (current_protect_state != PROTECT_STATE_NORMAL) {
            if (current_protect_state == PROTECT_STATE_PROTECT) {
                ST7735::draw_image(113, 18 + 21 + 21, ERRORRECTANGLE_WIDTH, ERRORRECTANGLE_HEIGHT, ErrorRectangle_data);
                ST7735::draw_string(113 + 5, 18 + 21 + 21, "OCP", ST7735::color_t(0x000000), error_background_color, DENGB16);
            } else if (current_protect_state == PROTECT_STATE_WARNING) {
                ST7735::draw_image(113, 18 + 21 + 21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, WarningRectangle_data);
                ST7735::draw_string(113 + 5, 18 + 21 + 21, "OCP", ST7735::color_t(0x000000), warning_background_color, DENGB16);
            }
        } else {
            ST7735::fill_rect(113, 18 + 21 + 21, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, background_color);
        }

        ST7735::sync_buffers();
        vTaskDelayUntil(&ticks, configTICK_RATE_HZ / fps);
    }
}

}
