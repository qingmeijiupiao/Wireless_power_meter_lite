/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕 UI 内部公共类型、配置和绘制工具声明
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <stdint.h>

#include "HXC_NVS.h"
#include "st7735.h"
#include "wifi_service.h"

namespace SCREEN {

static constexpr uint32_t DEFAULT_REFRESH_MS = 1000 / 30;

/** 背光设置固定为 5 档，便于单按键循环调整 */
static constexpr uint8_t BACKLIGHT_LEVEL_COUNT = 5;

/** 默认背光档位，取中间偏亮档 */
static constexpr uint8_t DEFAULT_BACKLIGHT_LEVEL = 3;

/**
 * @brief 屏幕页面 ID，数组顺序与单向翻页顺序保持一致
 */
enum class PageId : uint8_t {
    Dashboard,
    Battery,
    Curve,
    Wireless,
    Settings,
    Count,
};

/**
 * @brief 页面渲染模式
 */
enum class RenderMode : uint8_t {
    Normal,  /**< 周期刷新 */
    Full,    /**< 页面切换或状态变化后的完整刷新 */
};

/**
 * @brief 绘制全局编辑模式提示线
 */
void draw_edit_indicator();

/**
 * @brief 绘制通用页面标题
 * @param title 标题文本
 */
void draw_page_title(const char* title);

/**
 * @brief 归一化背光档位
 * @param level 背光档位，范围 1-5
 * @return 合法背光档位
 */
uint8_t normalize_backlight_level(uint8_t level);

/**
 * @brief 将 5 档背光映射为 ST7735 背光值
 * @param level 背光档位，范围 1-5
 * @return ST7735 背光值，范围 0-255
 */
uint8_t backlight_value_from_level(uint8_t level);

/**
 * @brief 获取已保存的 180 度旋转配置
 * @return true 表示 180 度旋转
 */
bool ui_config_get_rotation_180();

/**
 * @brief 保存 180 度旋转配置
 * @param enabled true 表示 180 度旋转
 */
void ui_config_set_rotation_180(bool enabled);

/**
 * @brief 获取已保存的背光档位
 * @return 背光档位，范围 1-5
 */
uint8_t ui_config_get_backlight_level();

/**
 * @brief 保存背光档位
 * @param level 背光档位，范围 1-5
 */
void ui_config_set_backlight_level(uint8_t level);

/**
 * @brief 获取 WiFi 模式显示文本
 * @param mode WiFiService 当前模式
 * @return 静态字符串
 */
const char* wifi_mode_text(WifiService::Mode mode);

} // namespace SCREEN

#endif
