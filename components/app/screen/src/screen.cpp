/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕任务入口实现，负责 ST7735 初始化、UIManager 启动和屏幕刷新主循环
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 19:10:43
 */
#include "screen.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "HXC_NVS.h"
#include "global_state.h"
#include "hardware.h"
#include "protect.h"
#include "power_output.h"
#include "st7735.h"
#include "ui_manager.h"
#include "start_logo.h"

namespace SCREEN {
namespace {

static constexpr const char* TAG = "screen_task";
Button main_button;
Button side_button;
HXC::NVS_DATA<uint32_t> start_logo_duration_ms("ui_logo_ms", DEFAULT_START_LOGO_DURATION_MS);

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

uint32_t get_start_logo_duration_ms() {
    return start_logo_duration_ms.read();
}

void set_start_logo_duration_ms(uint32_t duration_ms) {
    start_logo_duration_ms = duration_ms;
}

esp_err_t init_buttons() {
    main_button.bind_event(ButtonEvent::SHORT_PRESS, []() {
        if (!post_button_event(ButtonId::Main, ButtonEvent::SHORT_PRESS)) {
            PowerOutput::toggle(TAG);
        }
    });
    side_button.bind_event(ButtonEvent::SHORT_PRESS, []() {
        post_button_event(ButtonId::Side, ButtonEvent::SHORT_PRESS);
    });
    side_button.bind_event(ButtonEvent::DOUBLE_CLICK, []() {
        post_button_event(ButtonId::Side, ButtonEvent::DOUBLE_CLICK);
    });
    side_button.bind_event(ButtonEvent::LONG_PRESS, []() {
        post_button_event(ButtonId::Side, ButtonEvent::LONG_PRESS);
    });
    side_button.bind_event(ButtonEvent::SUPER_LONG_PRESS, []() {
        post_button_event(ButtonId::Side, ButtonEvent::SUPER_LONG_PRESS);
    });

    esp_err_t ret = main_button.setup(get_hardware_config().MAIN_BUTTON, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "main button init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = side_button.setup(get_hardware_config().SIDE_BUTTON, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "side button init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "buttons initialized");
    return ESP_OK;
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

    UIManager::instance().apply_saved_display_config();
    ST7735::fill_screen(ST7735::BLACK);
    const uint32_t logo_duration_ms = get_start_logo_duration_ms();
    if (logo_duration_ms > 0) {
        constexpr uint16_t START_LOGO_X = (ST7735::WIDTH - START_LOGO_WIDTH) / 2;
        constexpr uint16_t START_LOGO_Y = (ST7735::HEIGHT - START_LOGO_HEIGHT) / 2;
        ST7735::draw_image(START_LOGO_X, START_LOGO_Y, START_LOGO_WIDTH, START_LOGO_HEIGHT, start_logo_data);
    }
    ST7735::copy_buffers();
    ST7735::sync_buffers();

    const TickType_t logo_start_ticks = xTaskGetTickCount();
    while (!protect_init_ok() ||
           xTaskGetTickCount() - logo_start_ticks < pdMS_TO_TICKS(logo_duration_ms)) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!UIManager::instance().init()) {
        ESP_LOGE(TAG, "UI manager init failed");
        vTaskDelete(nullptr);
        return;
    }

    get_global_state().flags.bits.screen_initialized = true;

    ESP_LOGI(TAG, "Screen task started");
    while (true) {
        UIManager::instance().loop_once();
    }
}

} // namespace SCREEN
