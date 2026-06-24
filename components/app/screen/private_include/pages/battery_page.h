/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 累计电量页面声明
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#ifndef SCREEN_BATTERY_PAGE_H
#define SCREEN_BATTERY_PAGE_H

#include "core/page.h"

namespace SCREEN {

/**
 * @brief 电量页，显示实时测量值、输出状态、累计电量和计量时间。
 */
class BatteryPage final : public Page {
public:
    PageId id() const override;
    const char* title() const override;
    uint32_t refresh_interval_ms() const override;
    bool handle_button(ButtonId button, ButtonEvent event) override;
    void render(RenderMode mode) override;
};


} // namespace SCREEN

#endif // SCREEN_BATTERY_PAGE_H
