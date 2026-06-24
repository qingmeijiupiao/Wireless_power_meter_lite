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
    /** @brief `id` 接口。 */
    PageId      id() const override;
    /** @brief `title` 接口。 */
    const char* title() const override;
    /** @brief `refresh_interval_ms` 接口。 */
    uint32_t    refresh_interval_ms() const override;
    /** @brief `handle_button` 接口。 */
    bool        handle_button(ButtonId button, ButtonEvent event) override;
    /** @brief `render` 接口。 */
    void        render(RenderMode mode) override;
};

} // namespace SCREEN

#endif // SCREEN_BATTERY_PAGE_H
