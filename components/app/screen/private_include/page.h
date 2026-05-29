/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕页面抽象基类，定义页面生命周期、刷新周期、渲染和按键处理接口
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30
 */
#ifndef PAGE_H
#define PAGE_H

#include "Button.h"
#include "screen.h"
#include "ui_common.h"

namespace SCREEN {

/**
 * @brief 屏幕页面基类
 *
 * 页面只负责自身内容绘制和页面内按键行为。页面切换、默认按键行为、
 * 全局编辑提示和刷新节奏由 UIManager 统一管理。
 */
class Page {
public:
    virtual ~Page() = default;

    /**
     * @brief 获取页面 ID
     * @return 页面 ID
     */
    virtual PageId id() const = 0;

    /**
     * @brief 获取页面标题
     * @return 静态标题字符串
     */
    virtual const char* title() const = 0;

    /**
     * @brief 获取页面刷新周期
     * @return 刷新周期，单位 ms
     */
    virtual uint32_t refresh_interval_ms() const { return DEFAULT_REFRESH_MS; }

    /**
     * @brief 页面是否支持编辑模式
     * @return true 表示 UIManager 长按侧键时可进入编辑模式
     */
    virtual bool supports_edit_mode() const { return false; }

    /**
     * @brief 页面是否需要显示全局编辑提示
     * @return true 表示绘制全局编辑提示
     */
    virtual bool is_overlay_active() const { return false; }

    /** @brief 页面进入回调 */
    virtual void on_enter() {}

    /** @brief 页面退出回调 */
    virtual void on_exit() {}

    /** @brief UIManager 请求进入编辑模式 */
    virtual void on_edit_enter() {}

    /** @brief UIManager 请求退出编辑模式 */
    virtual void on_edit_exit() {}

    /**
     * @brief 处理页面内按键事件
     * @param button 按键 ID
     * @param event 按键事件
     * @return true 表示事件已被页面消费
     */
    virtual bool handle_button(ButtonId button, ButtonEvent event) {
        (void)button;
        (void)event;
        return false;
    }

    /**
     * @brief 渲染页面内容
     * @param mode 渲染模式
     */
    virtual void render(RenderMode mode) = 0;
};

} // namespace SCREEN

#endif
