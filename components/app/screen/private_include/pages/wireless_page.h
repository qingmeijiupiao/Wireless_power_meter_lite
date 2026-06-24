/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 无线状态页面声明
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#ifndef SCREEN_WIRELESS_PAGE_H
#define SCREEN_WIRELESS_PAGE_H

#include "core/page.h"
#include "esp_err.h"

namespace SCREEN {

/** @brief 无线页面，展示 WiFi、ESP-NOW 和配网状态。 */
class WirelessPage final : public Page {
  public:
    /** @brief 获取页面标识。 */
    PageId      id() const override;
    /** @brief 获取页面标题。 */
    const char* title() const override;
    /** @brief 获取刷新周期，单位 ms。 */
    uint32_t    refresh_interval_ms() const override;
    /** @brief 处理长按进入 AP 配网。 */
    bool        handle_button(ButtonId button, ButtonEvent event) override;
    /** @brief 绘制无线状态页面。 */
    void        render(RenderMode mode) override;

  private:
    esp_err_t last_result_ = ESP_OK; // 最近一次进入配网模式的执行结果
};

} // namespace SCREEN

#endif // SCREEN_WIRELESS_PAGE_H
