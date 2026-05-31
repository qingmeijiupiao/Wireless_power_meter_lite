/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕任务入口实现，负责 ST7735 初始化、UIManager 启动和屏幕刷新主循环
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30
 */
#include "screen.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "hardware.h"
#include "protect.h"
#include "st7735.h"
#include "ui_manager.h"

namespace SCREEN {
namespace {

static constexpr const char* TAG = "screen_task";

/**
 * @brief 从硬件配置生成 ST7735 初始化参数
 * @return ST7735 配置结构
 */
ST7735::Config make_lcd_config() {
    auto hardware_config = get_hardware_config();
    ST7735::Config cfg = {};
    cfg.sclk_io_num = hardware_config.TFT_SCL;
    cfg.mosi_io_num = hardware_config.TFT_SDA;
    cfg.cs_io_num = hardware_config.TFT_CS;
    cfg.dc_io_num = hardware_config.TFT_RS;
    cfg.rst_io_num = hardware_config.TFT_RST;
    cfg.bl_io_num = hardware_config.TFT_BLK;
    cfg.bl_active_state = hardware_config.TFT_BLK_ACTIVE_STATE;
    cfg.host_id = SPI2_HOST;
    return cfg;
}

} // namespace

bool post_button_event(ButtonId button, ButtonEvent event) {
    return UIManager::instance().post_button_event(button, event);
}

void screen_task(void* arg) {
    (void)arg;

    static ST7735::Config cfg = make_lcd_config();
    esp_err_t ret = ST7735::init(&cfg, ST7735::Rotation::Horizontal);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7735 init failed");
        vTaskDelete(nullptr);
        return;
    }

    if (!UIManager::instance().init()) {
        vTaskDelete(nullptr);
        return;
    }

    UIManager::instance().apply_saved_display_config();
    get_global_state().flags.bits.screen_initialized = true;
    ST7735::fill_screen(ST7735::BLACK);
    ST7735::copy_buffers();
    ST7735::sync_buffers();

    while (!protect_init_ok()) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGI(TAG, "Screen task started");
    while (true) {
        UIManager::instance().loop_once();
    }
}

} // namespace SCREEN
