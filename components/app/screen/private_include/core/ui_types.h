/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕 UI 核心枚举与轻量公共类型
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#ifndef SCREEN_UI_TYPES_H
#define SCREEN_UI_TYPES_H

#include <stdint.h>

namespace SCREEN {

static constexpr uint32_t DEFAULT_REFRESH_MS = 1000 / 30; // 默认页面刷新周期，单位 ms

/**
 * @brief 屏幕页面标识。
 *
 * 枚举仅用于稳定标识页面，不承担页面注册和翻页顺序定义。
 */
enum class PageId : uint8_t {
    Dashboard, /**< 实时测量主页 */
    Battery,   /**< 累计计量页面 */
    Curve,     /**< 历史曲线页面 */
    Wireless,  /**< 无线状态页面 */
    Settings,  /**< 设置页面 */
    Count,     /**< 页面数量，不代表有效页面 */
};

/**
 * @brief 页面渲染模式。
 */
enum class RenderMode : uint8_t {
    Normal, /**< 周期刷新 */
    Full,   /**< 页面切换或交互后的完整刷新 */
};

} // namespace SCREEN

#endif // SCREEN_UI_TYPES_H
