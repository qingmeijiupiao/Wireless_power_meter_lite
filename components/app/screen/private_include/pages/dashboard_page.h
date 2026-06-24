/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 实时测量主页声明
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#ifndef SCREEN_DASHBOARD_PAGE_H
#define SCREEN_DASHBOARD_PAGE_H

#include "core/page.h"
#include "protect.h"

namespace SCREEN {

/**
 * @brief 主页，显示实时电压、电流、功率、温度、输出和保护状态
 */
class DashboardPage final : public Page {
public:
    PageId id() const override;
    const char* title() const override;
    uint32_t refresh_interval_ms() const override;
    void render(RenderMode mode) override;

private:
    /**
     * @brief 绘制主页右侧保护状态标签
     * @param x 标签左上角 X 坐标
     * @param y 标签左上角 Y 坐标
     * @param text 标签文本
     * @param state 保护状态
     */
    void draw_protect_tag(uint16_t x, uint16_t y, const char* text, ProtectState_t state);
};


} // namespace SCREEN

#endif // SCREEN_DASHBOARD_PAGE_H
