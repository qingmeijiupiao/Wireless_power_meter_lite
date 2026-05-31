/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕应用页面实现，包含主页数据渲染、无线状态页、设置页和预留页面
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-31 11:27:02
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
#include "meter_a_logo.h"
#include "meter_circle_green.h"
#include "meter_circle_red.h"
#include "meter_v_logo.h"
#include "meter_w_logo.h"
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

/**
 * @brief 计算 10 的非负整数次幂。
 * @param exponent 指数。
 * @return 10 的 exponent 次幂。
 */
double pow10(uint8_t exponent) {
    double value = 1.0;
    while (exponent-- > 0) {
        value *= 10.0;
    }
    return value;
}

/**
 * @brief 按最大数字位数格式化绝对值，并在末尾附加单位。
 *
 * 小数点和单位不计入 max_digits。数值增大时会逐步减少小数位；
 * clamp 为 true 时，超出显示范围的值会封顶为全 9。
 *
 * @param line 输出缓冲区。
 * @param line_size 输出缓冲区大小。
 * @param value 待格式化数值。
 * @param unit 单位后缀。
 * @param max_digits 最大数字位数。
 * @param max_precision 最多保留的小数位数。
 * @param clamp 是否在超出显示范围时封顶。
 */
void format_fixed_digits(char* line, size_t line_size, double value, const char* unit,
                         uint8_t max_digits, uint8_t max_precision, bool clamp) {
    value = std::abs(value);
    int precision = max_precision;
    while (precision > 0) {
        const double rounding_limit = pow10(max_digits - precision) - 0.5 / pow10(precision);
        if (value < rounding_limit) {
            break;
        }
        precision--;
    }

    if (clamp && value >= pow10(max_digits) - 0.5) {
        snprintf(line, line_size, "%.*s%s", max_digits, "9999999999", unit);
        return;
    }
    snprintf(line, line_size, "%.*f%s", precision, value, unit);
}

/**
 * @brief 将秒数格式化为时分秒文本。
 * @param line 输出缓冲区。
 * @param line_size 输出缓冲区大小。
 * @param prefix 可选前缀，例如 "S:"；传入 nullptr 表示无前缀。
 * @param total_seconds 总秒数。
 */
void format_duration(char* line, size_t line_size, const char* prefix, uint64_t total_seconds) {
    snprintf(line, line_size, "%s%02llu:%02llu:%02llu",
             prefix == nullptr ? "" : prefix,
             static_cast<unsigned long long>(total_seconds / 3600),
             static_cast<unsigned long long>((total_seconds / 60) % 60),
             static_cast<unsigned long long>(total_seconds % 60));
}

} // namespace

/** @brief 返回主页 ID。 */
PageId DashboardPage::id() const {
    return PageId::Dashboard;
}

/** @brief 返回主页标题。 */
const char* DashboardPage::title() const {
    return "Main";
}

/** @brief 返回主页刷新周期。 */
uint32_t DashboardPage::refresh_interval_ms() const {
    return 1000 / 30;
}

/**
 * @brief 绘制主页实时测量值、运行时间、输出状态和保护状态。
 * @param mode 页面渲染模式，主页始终执行整屏重绘。
 */
void DashboardPage::render(RenderMode mode) {
    (void)mode;
    char temp_str[16];
    auto& global_state = get_global_state();
    auto& global_state_bits = global_state.global_state_bits;
    auto& protect_states = global_state.protect_states.states_bit;
    const float voltage = global_state.voltage_mV / 1000.0f;
    const float current = std::abs(global_state.current_uA / 1000000.0f);

    auto draw_static_layout = []() {
        ST7735::fill_screen(ST7735::BLACK);
        ST7735::draw_image(4, 4, STATIC_WIDTH, STATIC_HEIGHT, static_data);
        ST7735::fill_rect(106, 0, 2, 80, ST7735::YELLOW);
        ST7735::fill_rect(108, 13, 52, 2, ST7735::YELLOW);
    };

    auto draw_measurements = [&]() {
        snprintf(temp_str, sizeof(temp_str), "%.3fV", voltage);
        ST7735::draw_string(28, 4, temp_str, ST7735::color_t(0xef2a2a), ST7735::BLACK, DENGB20);
        snprintf(temp_str, sizeof(temp_str), "%.3fA", current);
        ST7735::draw_string(28, 27, temp_str, ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB20);
        snprintf(temp_str, sizeof(temp_str), "%.3fW", current * voltage);
        ST7735::draw_string(28, 49, temp_str, ST7735::color_t(0x003ED0), ST7735::BLACK, DENGB16);

        const float temperature = global_state.board_temperature / 100.0f;
        if (temperature >= 100.0f || temperature < 0.0f) {
            snprintf(temp_str, sizeof(temp_str), "%dC", static_cast<int>(temperature));
        } else {
            snprintf(temp_str, sizeof(temp_str), "%.1fC", temperature);
        }
        ST7735::draw_string(28, 69, temp_str, ST7735::color_t(0xb3261e), ST7735::BLACK, DENGB12);
    };

    auto draw_uptime = [&]() {
        const uint32_t total_seconds = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
        format_duration(temp_str, sizeof(temp_str), nullptr, total_seconds);
        ST7735::draw_string(111, 2, temp_str, ST7735::WHITE, ST7735::BLACK, DENGB12);
    };

    auto draw_output_state = [&]() {
        const bool enabled = global_state_bits.state_bit.out_put_state;
        ST7735::draw_image(62, 66,
                           enabled ? OPEN_WIDTH : CLOSE_WIDTH,
                           enabled ? OPEN_HEIGHT : CLOSE_HEIGHT,
                           enabled ? open_data : close_data);
    };

    auto draw_protect_states = [&]() {
        draw_protect_tag(113, 18, "OTP", protect_states.temperature_protect_state);
        ProtectState_t voltage_state = protect_states.high_voltage_protect_state;
        const char* voltage_text = "OVP";
        if (voltage_state == PROTECT_STATE_NORMAL) {
            voltage_state = protect_states.low_voltage_protect_state;
            voltage_text = "UVP";
        }
        draw_protect_tag(113, 39, voltage_text, voltage_state);
        draw_protect_tag(113, 60, "OCP", protect_states.current_protect_state);
    };

    draw_static_layout();
    draw_measurements();
    draw_uptime();
    draw_output_state();
    draw_protect_states();
}

/**
 * @brief 绘制主页右侧的单个保护状态标签。
 * @param x 标签左上角 X 坐标。
 * @param y 标签左上角 Y 坐标。
 * @param text 标签文本。
 * @param state 保护状态。
 */
void DashboardPage::draw_protect_tag(uint16_t x, uint16_t y, const char* text, ProtectState_t state) {
    if (state == PROTECT_STATE_NORMAL) {
        return;
    }

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

/** @brief 返回电量页 ID。 */
PageId BatteryPage::id() const {
    return PageId::Battery;
}

/** @brief 返回电量页标题。 */
const char* BatteryPage::title() const {
    return "Battery";
}

/** @brief 返回电量页刷新周期。 */
uint32_t BatteryPage::refresh_interval_ms() const {
    return 250;
}

/**
 * @brief 处理电量页按键，侧键长按时重置共享计量会话。
 * @param button 按键 ID。
 * @param event 按键事件。
 * @return true 表示事件已处理。
 */
bool BatteryPage::handle_button(ButtonId button, ButtonEvent event) {
    if (button != ButtonId::Side || event != ButtonEvent::LONG_PRESS) {
        return false;
    }

    EnergyMeter::reset();
    return true;
}

/**
 * @brief 绘制电量页实时状态、累计计量值和时间信息。
 * @param mode 页面渲染模式，电量页始终执行整屏重绘。
 */
void BatteryPage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);

    const EnergyMeter::Snapshot meter = EnergyMeter::snapshot();
    const int64_t meter_uwh = meter.energy_uwh;
    const int64_t meter_uah = meter.charge_uah;
    const auto& global_state = get_global_state();
    const float voltage = global_state.voltage_mV / 1000.0f;
    const float current = global_state.current_uA / 1000000.0f;
    const float power = voltage * current;
    const uint32_t system_seconds = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
    const uint64_t meter_seconds = meter.meter_time_ms / 1000;

    char line[32];

    auto draw_realtime_status = [&]() {
        ST7735::draw_image(2, 4, METER_V_LOGO_WIDTH, METER_V_LOGO_HEIGHT, meter_v_logo_data);
        format_fixed_digits(line, sizeof(line), voltage, "V", 3, 2, true);
        ST7735::draw_string(12, 5, line, ST7735::WHITE, ST7735::BLACK, DENGB12);

        ST7735::draw_image(48, 4, METER_A_LOGO_WIDTH, METER_A_LOGO_HEIGHT, meter_a_logo_data);
        format_fixed_digits(line, sizeof(line), current, "A", 3, 2, true);
        ST7735::draw_string(58, 5, line, ST7735::WHITE, ST7735::BLACK, DENGB12);

        ST7735::draw_image(93, 4, METER_W_LOGO_WIDTH, METER_W_LOGO_HEIGHT, meter_w_logo_data);
        format_fixed_digits(line, sizeof(line), power, "W", 3, 2, true);
        ST7735::draw_string(106, 5, line, ST7735::WHITE, ST7735::BLACK, DENGB12);

        const bool output_enabled = global_state.global_state_bits.state_bit.out_put_state;
        ST7735::draw_image(145, 4,
                           output_enabled ? METER_CIRCLE_GREEN_WIDTH : METER_CIRCLE_RED_WIDTH,
                           output_enabled ? METER_CIRCLE_GREEN_HEIGHT : METER_CIRCLE_RED_HEIGHT,
                           output_enabled ? meter_circle_green_data : meter_circle_red_data);
    };

    auto draw_meter_values = [&]() {
        ST7735::draw_image(2, 18, WH_LOGO_WIDTH, WH_LOGO_HEIGHT, wh_logo_data);
        format_fixed_digits(line, sizeof(line), meter_uwh / 1000.0, "mWh", 6, 3, false);
        ST7735::draw_string(34, 20, line, ST7735::color_t(0x003ED0), ST7735::BLACK, DENGB20);

        ST7735::draw_image(2, 43, AH_LOGO_WIDTH, AH_LOGO_HEIGHT, ah_logo_data);
        format_fixed_digits(line, sizeof(line), meter_uah / 1000.0, "mAh", 6, 3, false);
        ST7735::draw_string(34, 45, line, ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB20);
    };

    auto draw_time_values = [&]() {
        format_duration(line, sizeof(line), "S:", system_seconds);
        ST7735::draw_string(2, 68, line, ST7735::color_t(0x2FC9EC), ST7735::BLACK, DENGB12);
        format_duration(line, sizeof(line), "M:", meter_seconds);
        ST7735::draw_string(90, 68, line, ST7735::color_t(0x1EF851), ST7735::BLACK, DENGB12);
    };

    draw_realtime_status();
    draw_meter_values();
    draw_time_values();
}

/** @brief 返回曲线页 ID。 */
PageId CurvePage::id() const {
    return PageId::Curve;
}

/** @brief 返回曲线页标题。 */
const char* CurvePage::title() const {
    return "Curve";
}

/** @brief 返回曲线页刷新周期。 */
uint32_t CurvePage::refresh_interval_ms() const {
    return 200;
}

/**
 * @brief 绘制曲线功能占位页。
 * @param mode 页面渲染模式。
 */
void CurvePage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);
    draw_page_title("Curve");
    ST7735::draw_string(8, 32, "Chart page", ST7735::WHITE, ST7735::BLACK, DENGB16);
    ST7735::draw_string(8, 54, "reserved", ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
}

/** @brief 返回无线页 ID。 */
PageId WirelessPage::id() const {
    return PageId::Wireless;
}

/** @brief 返回无线页标题。 */
const char* WirelessPage::title() const {
    return "Wireless";
}

/** @brief 返回无线页刷新周期。 */
uint32_t WirelessPage::refresh_interval_ms() const {
    return 500;
}

/**
 * @brief 处理无线页按键，侧键长按时进入 AP 配网模式。
 * @param button 按键 ID。
 * @param event 按键事件。
 * @return true 表示事件已处理。
 */
bool WirelessPage::handle_button(ButtonId button, ButtonEvent event) {
    if (button != ButtonId::Side || event != ButtonEvent::LONG_PRESS) {
        return false;
    }

    last_result_ = WifiService::start_provision_ap();
    return true;
}

/**
 * @brief 按当前网络模式绘制无线状态页。
 * @param mode 页面渲染模式，无线页始终执行整屏重绘。
 */
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

    auto draw_header = [&]() {
        ST7735::draw_string(4, 3, "WiFi", ST7735::WHITE, ST7735::BLACK, DENGB16);
        ST7735::fill_rect(0, 21, ST7735::WIDTH, 1, ST7735::color_t(0x303030));
        ST7735::fill_rect(126, 3, 30, 15, ST7735::color_t(0x202020));
        ST7735::draw_string(132, 5, mode_text, mode_color, ST7735::color_t(0x202020), DENGB12);
    };

    auto draw_provisioning = [&]() {
        ST7735::draw_string(4, 27, "CONFIG AP", ST7735::color_t(0x2FC9EC), ST7735::BLACK, DENGB12);
        snprintf(line, sizeof(line), "%.20s", WifiService::get_ap_ssid());
        ST7735::draw_string(4, 44, line, ST7735::WHITE, ST7735::BLACK, DENGB12);
        ST7735::draw_string(4, 61, "192.168.4.1", ST7735::color_t(0x1ef851), ST7735::BLACK, DENGB12);
    };

    auto draw_sta = [&]() {
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
    };

    auto draw_error = [&]() {
        snprintf(line, sizeof(line), "ERR %.20s", esp_err_to_name(last_result_));
        ST7735::draw_string(4, 33, line, ST7735::color_t(0xef2a2a), ST7735::BLACK, DENGB12);
        ST7735::draw_string(4, 54, "Hold side: retry", ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
    };

    auto draw_off = []() {
        ST7735::draw_string(4, 33, "WiFi service OFF", ST7735::WHITE, ST7735::BLACK, DENGB12);
        ST7735::draw_string(4, 54, "Hold side: AP", ST7735::color_t(0x808080), ST7735::BLACK, DENGB12);
    };

    draw_header();
    if (provisioning) {
        draw_provisioning();
    } else if (wifi_mode == WifiService::Mode::STA) {
        draw_sta();
    } else if (last_result_ != ESP_OK) {
        draw_error();
    } else {
        draw_off();
    }
}

/** @brief 返回设置页 ID。 */
PageId SettingsPage::id() const {
    return PageId::Settings;
}

/** @brief 返回设置页标题。 */
const char* SettingsPage::title() const {
    return "Settings";
}

/** @brief 返回设置页刷新周期。 */
uint32_t SettingsPage::refresh_interval_ms() const {
    return 200;
}

/** @brief 声明设置页支持菜单编辑模式。 */
bool SettingsPage::supports_edit_mode() const {
    return true;
}

/** @brief 返回设置页菜单是否处于激活状态。 */
bool SettingsPage::is_overlay_active() const {
    return mode_ != Mode::View;
}

/** @brief 进入设置菜单。 */
void SettingsPage::on_edit_enter() {
    mode_ = Mode::Menu;
}

/** @brief 退出设置菜单。 */
void SettingsPage::on_edit_exit() {
    mode_ = Mode::View;
}

/**
 * @brief 处理设置菜单内的选择、修改和退出操作。
 * @param button 按键 ID。
 * @param event 按键事件。
 * @return true 表示事件已由设置页消费。
 */
bool SettingsPage::handle_button(ButtonId button, ButtonEvent event) {
    if (mode_ == Mode::View) {
        return false;
    }

    if (button == ButtonId::Side && event == ButtonEvent::SHORT_PRESS) {
        selected_ = (selected_ + 1) % ITEM_COUNT;
        return true;
    }

    if (button == ButtonId::Main && event == ButtonEvent::SHORT_PRESS) {
        adjust_selected_item();
        return true;
    }

    if (button == ButtonId::Side && event == ButtonEvent::LONG_PRESS) {
        mode_ = Mode::View;
        return true;
    }

    return true;
}

/**
 * @brief 绘制设置页图标和当前可见菜单项。
 * @param mode 页面渲染模式，设置页始终执行整屏重绘。
 */
void SettingsPage::render(RenderMode mode) {
    (void)mode;
    ST7735::fill_screen(ST7735::BLACK);
    ST7735::draw_image(2, 16, SETTINGS_LOGO_WIDTH, SETTINGS_LOGO_HEIGHT, settings_logo_data);

    auto draw_menu_rows = [&]() {
        for (uint8_t row = 0; row < VISIBLE_ROWS; row++) {
            const uint8_t item = (selected_ + row) % ITEM_COUNT;
            const uint16_t y = 10 + row * 17;
            const bool selected = row == 0 && mode_ != Mode::View;
            const ST7735::color_t foreground = selected ? ST7735::BLACK : ST7735::WHITE;
            const ST7735::color_t background = selected ? ST7735::YELLOW : ST7735::BLACK;
            if (selected) {
                ST7735::fill_rect(54, y - 1, 106, 15, background);
            }
            ST7735::draw_string(56, y, item_name(item), foreground, background, DENGB12);
            ST7735::draw_string(130, y, item_value(item), foreground, background, DENGB12);
        }
    };

    draw_menu_rows();
}

/** @brief 从 NVS 加载设置页使用的显示配置。 */
void SettingsPage::load_config() {
    rotation_180_ = ui_config_get_rotation_180();
    backlight_level_ = ui_config_get_backlight_level();
}

/**
 * @brief 返回设置项名称。
 * @param item 设置项索引。
 * @return 设置项显示名称。
 */
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

/**
 * @brief 返回设置项当前值的显示文本。
 * @param item 设置项索引。
 * @return 设置项值文本。
 */
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

/** @brief 修改当前选中的设置项。 */
void SettingsPage::adjust_selected_item() {
    switch (selected_) {
        case Rotate180:
            rotation_180_ = !rotation_180_;
            ui_config_set_rotation_180(rotation_180_);
            ST7735::set_rotation(rotation_180_ ? ST7735::Rotation::HorizontalMirror : ST7735::Rotation::Horizontal);
            break;
        case Backlight:
            backlight_level_++;
            if (backlight_level_ > BACKLIGHT_LEVEL_COUNT) {
                backlight_level_ = 1;
            }
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
