/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕方向、背光映射和 NVS 配置持久化实现
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24 22:55:33
 */
#include "config/display_config.h"

namespace SCREEN {
namespace {

HXC::NVS_DATA<uint8_t> ui_rotation_180("ui_rot", 0);
HXC::NVS_DATA<uint8_t> ui_backlight_level("ui_bl", DEFAULT_BACKLIGHT_LEVEL);

} // namespace

uint8_t normalize_backlight_level(uint8_t level) {
    if (level < 1 || level > BACKLIGHT_LEVEL_COUNT) {
        return DEFAULT_BACKLIGHT_LEVEL;
    }
    return level;
}

uint8_t backlight_value_from_level(uint8_t level) {
    // 将 1-n 档均匀映射到 0-255，最低档仍保持可见亮度。
    return static_cast<uint8_t>((255 * normalize_backlight_level(level)) / BACKLIGHT_LEVEL_COUNT);
}

bool ui_config_get_rotation_180() {
    return ui_rotation_180.read() != 0;
}

esp_err_t ui_config_set_rotation_180(bool enabled) {
    return ui_rotation_180.set(enabled ? 1 : 0);
}

uint8_t ui_config_get_backlight_level() {
    return normalize_backlight_level(ui_backlight_level.read());
}

esp_err_t ui_config_set_backlight_level(uint8_t level) {
    return ui_backlight_level.set(normalize_backlight_level(level));
}

} // namespace SCREEN
