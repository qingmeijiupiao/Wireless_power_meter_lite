/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕应用页面实现，包含主页数据渲染、无线状态页、设置页和预留页面
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-31 01:12:56
 */
#include "pages.h"

#include <cmath>
#include <cstdio>

#include "DENGB12.h"
#include "DENGB16.h"
#include "DENGB20.h"
#include "energy_meter.h"
#include "ErrorRectangle.h"
#include "WarningRectangle.h"
#include "can_callback.h"
#include "can_resistor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global_state.h"
#include "settings_logo.h"
#include "st7735.h"
#include "ui_close.h"
#include "ui_open.h"
#include "ui_static.h"
#include "wifi_service.h"
#include "ah_logo.h"
#include "wh_logo.h"

namespace SCREEN {
namespace {

constexpr uint32_t CAN_BAUDRATES[] = {
    1_Mbps,
    500_Kbps,
    250_Kbps,
    125_Kbps,
};

void format_meter_line(char* line, size_t line_size, int64_t value_u, const char* unit) {
    const double value_m = std::abs(value_u / 1000.0);
    int precision = 0;
    if (value_m < 1000.0) {
        precision = 3;
    } else if (value_m < 10000.0) {
        precision = 2;
    } else if (value_m < 100000.0) {
        precision = 1;
    }
    snprintf(line, line_size, "%.*f%s", precision, value_m, unit);
}

} // namespace

PageId DashboardPage::id() const {
    return PageId::Dashboard;
}

const char* DashboardPage::title() const {
    return "Main";
}

uint32_t DashboardPage::refresh_interval_ms() const {
    return 1000 / 30;
}

void DashboardPage::render(RenderMode mode) {
    (void)mode;

    // 主页元素紧凑且包含静态图标，当前采用整屏重画，避免双缓冲残留。
    ST7735::fill_screen(ST7735::BLACK);
    ST7735::draw_image(4, 4, STATIC_WIDTH, STATIC_HEIGHT, static_data);
    ST7735::fill_rect(106, 0, 2, 80, ST7735::YELLOW);
    ST7735::fill_rect(108, 13, 52, 2, ST7735::YELLOW);

    char temp_str[16];
    auto& global_state = get_global_state();
    auto& global_state_bits = global_state.global_state_bits;
    auto& protect_states = global_state.protect_states.states_bit;

    float voltage = global_state.voltage_mV / 1000.0f;
    float current = std::abs(global_state.current_uA / 1000000.0f);
    snprintf(temp_str, sizeof(temp_str), "%.3fV", voltage);
    ST7735::draw_string(28, 4, temp_str, ST7735::color_t(0xef2a2a), ST7735::BLACK, DENGB20);
    snprintf(temp_str, sizeof(temp_str), "%.3fA", current);
    ST7735::draw_string(28, 27, temp_str, ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB20);

    snprintf(temp_str, sizeof(temp_str), "%.3fW", current * voltage);
    ST7735::draw_string(28, 49, temp_str, ST7735::color_t(0x003ED0), ST7735::BLACK, DENGB16);

    float temperature = global_state.board_temperature / 100.0f;
    if (temperature >= 100.0f || temperature < 0.0f) {
        snprintf(temp_str, sizeof(temp_str), "%dC", static_cast<int>(temperature));
    } else {
        snprintf(temp_str, sizeof(temp_str), "%.1fC", temperature);
    }
    ST7735::draw_string(28, 69, temp_str, ST7735::color_t(0xb3261e), ST7735::BLACK, DENGB12);

    uint32_t total_seconds = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
    uint32_t hour = total_seconds / 3600;
    uint32_t minute = (total_seconds / 60) % 60;
    uint32_t second = total_seconds % 60;
    snprintf(temp_str, sizeof(temp_str), "%02lu:%02lu:%02lu",
             static_cast<unsigned long>(hour),
             static_cast<unsigned long>(minute),
             static_cast<unsigned long>(second));
    ST7735::draw_string(111, 2, temp_str, ST7735::WHITE, ST7735::BLACK, DENGB12);

    if (global_state_bits.state_bit.out_put_state) {
        ST7735::draw_image(62, 66, OPEN_WIDTH, OPEN_HEIGHT, open_data);
    } else {
        ST7735::draw_image(62, 66, CLOSE_WIDTH, CLOSE_HEIGHT, close_data);
    }

    draw_protect_tag(113, 18, "OTP", protect_states.temperature_protect_state);
    ProtectState_t voltage_state = protect_states.high_voltage_protect_state;
    const char* voltage_text = "OVP";
    if (voltage_state == PROTECT_STATE_NORMAL) {
        voltage_state = protect_states.low_voltage_protect_state;
        voltage_text = "UVP";
    }
    draw_protect_tag(113, 39, voltage_text, voltage_state);
    draw_protect_tag(113, 60, "OCP", protect_states.current_protect_state);
}

void DashboardPage::draw_protect_tag(uint16_t x, uint16_t y, const char* text, ProtectState_t state) {
    if (state == PROTECT_STATE_NORMAL) {
        return;
    }

    // 保护标签背景来自资源图片，文本使用背景色参与抗锯齿混色。
    ST7735::color_t warning_background_color;
    warning_background_color.set_color_raw(0xFE60);
    ST7735::color_t error_background_color;
    error_background_color.set_color_raw(0xB123);

    if (state == PROTECT_STATE_PROTECT) {
        ST7735::draw_image(x, y, ERRORRECTANGLE_WIDTH, ERRORRECTANGLE_HEIGHT, ErrorRectangle_data);
        ST7735::draw_string(x + 5, y + 2, text, ST7735::BLACK, error_background_color, DENGB16);
    } else if (state == PROTECT_STATE_WARNING) {
        ST7735::draw_image(x, y, WARNINGRECTANGLE_WIDTH, WARNINGRECTANGLE_HEIGHT, WarningRectangle_data);
        ST7735::draw_string(x + 5, y + 2, text, ST7735::BLACK, warning_background_color, DENGB16);
    }
}

PageId BatteryPage::id() const {
    return PageId::Battery;
}

const char* BatteryPage::title() const {
    return "Battery";
}

uint32_t BatteryPage::refresh_interval_ms() const {
    return 250;
}

bool BatteryPage::handle_button(ButtonId button, ButtonEvent event) {
    if (button != ButtonId::Side || event != ButtonEvent::LONG_PRESS) {
        return false;
    }

    EnergyMeter::reset();
    return true;
}

void BatteryPage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);

    const EnergyMeter::Snapshot meter = EnergyMeter::snapshot();
    const int64_t meter_uwh = meter.energy_uwh;
    const int64_t meter_uah = meter.charge_uah;
    const uint32_t system_seconds = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
    const uint64_t meter_seconds = meter.meter_time_ms / 1000;

    char line[32];
    ST7735::draw_image(2, 8, WH_LOGO_WIDTH, WH_LOGO_HEIGHT, wh_logo_data);

    ST7735::draw_image(2, 38, AH_LOGO_WIDTH, AH_LOGO_HEIGHT, ah_logo_data);

    format_meter_line(line, sizeof(line), meter_uwh, "mWh");
    ST7735::draw_string(34, 10, line, ST7735::color_t(0x003ED0), ST7735::BLACK, DENGB20);
    format_meter_line(line, sizeof(line), meter_uah, "mAh");
    ST7735::draw_string(34, 40, line, ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB20);

    //time
    snprintf(line, sizeof(line), "S:%02lu:%02lu:%02lu",
             static_cast<unsigned long>(system_seconds / 3600),
             static_cast<unsigned long>((system_seconds / 60) % 60),
             static_cast<unsigned long>(system_seconds % 60));
    ST7735::draw_string(2, 68, line, ST7735::color_t(0x2FC9EC), ST7735::BLACK, DENGB12);
    snprintf(line, sizeof(line), "M:%02lu:%02lu:%02lu",
             static_cast<unsigned long>(meter_seconds / 3600),
             static_cast<unsigned long>((meter_seconds / 60) % 60),
             static_cast<unsigned long>(meter_seconds % 60));
    ST7735::draw_string(90, 68, line, ST7735::color_t(0x1EF851), ST7735::BLACK, DENGB12);
}

PageId CurvePage::id() const {
    return PageId::Curve;
}

const char* CurvePage::title() const {
    return "Curve";
}

uint32_t CurvePage::refresh_interval_ms() const {
    return 200;
}

void CurvePage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);
    draw_page_title("Curve");
    ST7735::draw_string(8, 32, "Chart page", ST7735::WHITE, ST7735::BLACK, DENGB16);
    ST7735::draw_string(8, 54, "reserved", ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
}

PageId WirelessPage::id() const {
    return PageId::Wireless;
}

const char* WirelessPage::title() const {
    return "Wireless";
}

uint32_t WirelessPage::refresh_interval_ms() const {
    return 500;
}

bool WirelessPage::handle_button(ButtonId button, ButtonEvent event) {
    if (button != ButtonId::Side || event != ButtonEvent::LONG_PRESS) {
        return false;
    }

    // 无线页独占侧键长按：进入 AP 配网模式，短按仍交给 UIManager 翻页。
    last_result_ = WifiService::start_provision_ap();
    return true;
}

void WirelessPage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);

    char line[40];
    const WifiService::Mode wifi_mode = WifiService::get_mode();
    const bool provisioning = WifiService::is_provisioning();
    const char* mode_text = provisioning ? "AP" : wifi_mode_text(wifi_mode);
    ST7735::color_t mode_color = wifi_mode == WifiService::Mode::STA
        ? ST7735::color_t(0x1ef851)
        : ST7735::color_t(0x2FC9EC);
    if (last_result_ != ESP_OK && wifi_mode != WifiService::Mode::STA && !provisioning) {
        mode_text = "ERR";
        mode_color = ST7735::color_t(0xef2a2a);
    }

    ST7735::draw_string(4, 3, "WiFi", ST7735::WHITE, ST7735::BLACK, DENGB16);
    ST7735::fill_rect(0, 21, ST7735::WIDTH, 1, ST7735::color_t(0x303030));
    ST7735::fill_rect(126, 3, 30, 15, ST7735::color_t(0x202020));
    ST7735::draw_string(132, 5, mode_text, mode_color, ST7735::color_t(0x202020), DENGB12);

    if (provisioning) {
        ST7735::draw_string(4, 27, "CONFIG AP", ST7735::color_t(0x2FC9EC), ST7735::BLACK, DENGB12);
        snprintf(line, sizeof(line), "%.20s", WifiService::get_ap_ssid());
        ST7735::draw_string(4, 44, line, ST7735::WHITE, ST7735::BLACK, DENGB12);
        ST7735::draw_string(4, 61, "192.168.4.1", ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB12);
    } else if (wifi_mode == WifiService::Mode::STA) {
        WifiService::Config cfg = WifiService::get_config();
        IP_t ip = WifiService::get_ip();
        uint8_t channel = 0;
        WifiService::get_channel(&channel);
        snprintf(line, sizeof(line), "%.20s", cfg.ssid[0] == '\0' ? "STA connected" : cfg.ssid);
        ST7735::draw_string(4, 26, line, ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB12);
        snprintf(line, sizeof(line), "%u.%u.%u.%u",
                 static_cast<unsigned>(ip.octet1),
                 static_cast<unsigned>(ip.octet2),
                 static_cast<unsigned>(ip.octet3),
                 static_cast<unsigned>(ip.octet4));
        ST7735::draw_string(4, 42, line, ST7735::color_t(0x2FC9EC), ST7735::BLACK, DENGB12);

        const uint8_t signal = WifiService::get_signal_percent();
        snprintf(line, sizeof(line), "%u%%", static_cast<unsigned>(signal));
        ST7735::draw_string(4, 59, line, ST7735::WHITE, ST7735::BLACK, DENGB12);
        ST7735::fill_rect(36, 63, 70, 4, ST7735::color_t(0x303030));
        ST7735::fill_rect(36, 63, static_cast<uint16_t>(70 * signal / 100), 4, ST7735::color_t(0x1ef851));
        snprintf(line, sizeof(line), "CH %u", static_cast<unsigned>(channel));
        ST7735::draw_string(120, 59, line, ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
    } else if (last_result_ != ESP_OK) {
        snprintf(line, sizeof(line), "ERR %.20s", esp_err_to_name(last_result_));
        ST7735::draw_string(4, 33, line, ST7735::color_t(0xef2a2a), ST7735::BLACK, DENGB12);
        ST7735::draw_string(4, 54, "Hold side: retry", ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
    } else {
        ST7735::draw_string(4, 33, "WiFi service OFF", ST7735::WHITE, ST7735::BLACK, DENGB12);
        ST7735::draw_string(4, 54, "Hold side: AP", ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
    }
}

PageId SettingsPage::id() const {
    return PageId::Settings;
}

const char* SettingsPage::title() const {
    return "Settings";
}

uint32_t SettingsPage::refresh_interval_ms() const {
    return 200;
}

bool SettingsPage::supports_edit_mode() const {
    return true;
}

bool SettingsPage::is_overlay_active() const {
    return mode_ != Mode::View;
}

void SettingsPage::on_edit_enter() {
    mode_ = Mode::Menu;
}

void SettingsPage::on_edit_exit() {
    mode_ = Mode::View;
}

bool SettingsPage::handle_button(ButtonId button, ButtonEvent event) {
    if (mode_ == Mode::View) {
        return false;
    }

    if (button == ButtonId::Side && event == ButtonEvent::SHORT_PRESS) {
        // 菜单态短按只移动选中项，避免误修改配置。
        selected_ = (selected_ + 1) % ITEM_COUNT;
        return true;
    }

    if (button == ButtonId::Main && event == ButtonEvent::SHORT_PRESS) {
        // 所有设置项统一使用大按键修改，无二级编辑状态。
        adjust_selected_item();
        return true;
    }

    if (button == ButtonId::Side && event == ButtonEvent::LONG_PRESS) {
        mode_ = Mode::View;
        return true;
    }

    return true;
}

void SettingsPage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);
    ST7735::draw_image(2, 16, SETTINGS_LOGO_WIDTH, SETTINGS_LOGO_HEIGHT, settings_logo_data);

    // 左侧保留设置图标，右侧一次显示 4 行，第一行始终是当前选中的设置项。
    for (uint8_t row = 0; row < VISIBLE_ROWS; row++) {
        uint8_t item = (selected_ + row) % ITEM_COUNT;
        uint16_t y = 10 + row * 17;
        bool selected = row == 0 && mode_ != Mode::View;
        ST7735::color_t foreground = selected ? ST7735::BLACK : ST7735::WHITE;
        ST7735::color_t background = selected ? ST7735::YELLOW : ST7735::BLACK;
        if (selected) {
            ST7735::fill_rect(54, y - 1, 106, 15, background);
        }
        ST7735::draw_string(56, y, item_name(item), foreground, background, DENGB12);
        ST7735::draw_string(130, y, item_value(item), foreground, background, DENGB12);
    }
}

void SettingsPage::load_config() {
    rotation_180_ = ui_config_get_rotation_180();
    backlight_level_ = ui_config_get_backlight_level();
}

const char* SettingsPage::item_name(uint8_t item) const {
    switch (item) {
        case Rotate180:
            return "Rotate";
        case Backlight:
            return "Bright";
        case WifiBoot:
            return "WiFi boot";
        case ProtectBypass:
            return "Protect";
        case CanBaudrate:
            return "CAN baud";
        case CanTerm:
            return "CAN term";
        default:
            return "";
    }
}

const char* SettingsPage::item_value(uint8_t item) {
    switch (item) {
        case Rotate180:
            return rotation_180_ ? "180" : "0";
        case Backlight:
            snprintf(value_buf_, sizeof(value_buf_), "%u/5", static_cast<unsigned>(backlight_level_));
            return value_buf_;
        case WifiBoot:
            return WifiService::is_web_enabled_on_boot() ? "ON" : "OFF";
        case ProtectBypass:
            return protect_is_bypassed() ? "OFF" : "ON";
        case CanBaudrate:
            switch (CanCallback::CAN_BAUDRATE.read()) {
                case 1_Mbps:
                    return "1M";
                case 500_Kbps:
                    return "500K";
                case 250_Kbps:
                    return "250K";
                case 125_Kbps:
                    return "125K";
                default:
                    return "Other";
            }
        case CanTerm:
            return CanResistor::instance().get() ? "ON" : "OFF";
        default:
            return "";
    }
}

void SettingsPage::adjust_selected_item() {
    switch (selected_) {
        case Rotate180:
            rotation_180_ = !rotation_180_;
            // 旋转立即生效；页面下次渲染仍使用 160x80 逻辑坐标。
            ui_config_set_rotation_180(rotation_180_);
            ST7735::set_rotation(rotation_180_ ? ST7735::Rotation::HorizontalMirror : ST7735::Rotation::Horizontal);
            break;
        case Backlight:
            backlight_level_++;
            if (backlight_level_ > BACKLIGHT_LEVEL_COUNT) {
                backlight_level_ = 1;
            }
            // 按需求每次调整立即保存 NVS，后续如需减少 Flash 写入可在此处集中改为延迟提交。
            ui_config_set_backlight_level(backlight_level_);
            ST7735::set_backlight(backlight_value_from_level(backlight_level_));
            break;
        case WifiBoot: {
            bool enabled = !WifiService::is_web_enabled_on_boot();
            WifiService::set_web_enabled_on_boot(enabled);
            break;
        }
        case ProtectBypass:
            protect_set_bypassed(!protect_is_bypassed());
            break;
        case CanBaudrate: {
            uint32_t current = CanCallback::CAN_BAUDRATE.read();
            uint32_t next = CAN_BAUDRATES[0];
            for (size_t i = 0; i < sizeof(CAN_BAUDRATES) / sizeof(CAN_BAUDRATES[0]); ++i) {
                if (CAN_BAUDRATES[i] == current) {
                    next = CAN_BAUDRATES[(i + 1) % (sizeof(CAN_BAUDRATES) / sizeof(CAN_BAUDRATES[0]))];
                    break;
                }
            }
            CanCallback::CAN_BAUDRATE = next;
            break;
        }
        case CanTerm:
            CanResistor::instance().toggle();
            break;
        default:
            break;
    }
}

} // namespace SCREEN
