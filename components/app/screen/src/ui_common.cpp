/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕 UI 内部公共工具实现，包含通用绘制、背光档位映射和 UI 配置持久化
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30
 */
#include "ui_common.h"

#include "DENGB16.h"

namespace SCREEN {
namespace {

HXC::NVS_DATA<uint8_t> ui_rotation_180("ui_rot", 0);
HXC::NVS_DATA<uint8_t> ui_backlight_level("ui_bl", DEFAULT_BACKLIGHT_LEVEL);

} // namespace

void draw_edit_indicator() {
    // 全局编辑态只用顶部 1px 黄线提示，避免占用小屏内容区域。
    ST7735::fill_rect(0, 0, ST7735::WIDTH, 1, ST7735::YELLOW);
}

void draw_page_title(const char* title) {
    // 统一标题栏高度为 22px，下方灰线作为内容区分隔。
    ST7735::draw_string(4, 3, title, ST7735::WHITE, ST7735::BLACK, DENGB16);
    ST7735::fill_rect(0, 22, ST7735::WIDTH, 1, ST7735::color_t(0x303030));
}

uint8_t normalize_backlight_level(uint8_t level) {
    if (level < 1 || level > BACKLIGHT_LEVEL_COUNT) {
        return DEFAULT_BACKLIGHT_LEVEL;
    }
    return level;
}

uint8_t backlight_value_from_level(uint8_t level) {
    // 将 1-5 档均匀映射到 0-255，最低档仍保持可见亮度。
    return static_cast<uint8_t>((255 * normalize_backlight_level(level)) / BACKLIGHT_LEVEL_COUNT);
}

bool ui_config_get_rotation_180() {
    return ui_rotation_180.read() != 0;
}

void ui_config_set_rotation_180(bool enabled) {
    ui_rotation_180 = enabled ? 1 : 0;
}

uint8_t ui_config_get_backlight_level() {
    return normalize_backlight_level(ui_backlight_level.read());
}

void ui_config_set_backlight_level(uint8_t level) {
    ui_backlight_level = normalize_backlight_level(level);
}

const char* wifi_mode_text(WifiService::Mode mode) {
    switch (mode) {
        case WifiService::Mode::OFF:
            return "OFF";
        case WifiService::Mode::STA:
            return "STA";
        case WifiService::Mode::AP_PROVISION:
            return "AP";
        default:
            return "UNK";
    }
}

} // namespace SCREEN
