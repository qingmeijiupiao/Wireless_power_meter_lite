/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 静态页面实例与默认翻页顺序注册实现
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#include "core/page_registry.h"

#include "pages/battery_page.h"
#include "pages/curve/curve_page.h"
#include "pages/dashboard_page.h"
#include "pages/settings/settings_page.h"
#include "pages/wireless_page.h"

namespace SCREEN {
namespace {

DashboardPage dashboard_page; // 实时测量主页实例
BatteryPage   battery_page;   // 累计电量页面实例
CurvePage     curve_page;     // 历史曲线页面实例
WirelessPage  wireless_page;  // 无线状态页面实例
SettingsPage  settings_page;  // 设置页面实例

Page* pages[] = {
    &dashboard_page, &battery_page, &curve_page, &wireless_page, &settings_page,
}; // 默认翻页顺序

static_assert(sizeof(pages) / sizeof(pages[0]) == static_cast<size_t>(PageId::Count),
              "Page registry must contain every PageId");

} // namespace

PageRegistry get_page_registry() {
    return {
        .pages = pages,
        .count = sizeof(pages) / sizeof(pages[0]),
    };
}

} // namespace SCREEN
