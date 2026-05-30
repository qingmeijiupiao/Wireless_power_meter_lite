/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕应用页面声明，包含主页、无线页、设置页及后续页面占位
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30
 */
#ifndef PAGES_H
#define PAGES_H

#include "esp_err.h"
#include "page.h"
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

/**
 * @brief 电量页占位实现
 */
class BatteryPage final : public Page {
public:
    PageId id() const override;
    const char* title() const override;
    uint32_t refresh_interval_ms() const override;
    bool handle_button(ButtonId button, ButtonEvent event) override;
    void render(RenderMode mode) override;
};

/**
 * @brief 曲线页占位实现
 */
class CurvePage final : public Page {
public:
    PageId id() const override;
    const char* title() const override;
    uint32_t refresh_interval_ms() const override;
    void render(RenderMode mode) override;
};

/**
 * @brief 无线页，显示 WiFi 状态并支持长按进入 AP 配网
 */
class WirelessPage final : public Page {
public:
    PageId id() const override;
    const char* title() const override;
    uint32_t refresh_interval_ms() const override;
    bool handle_button(ButtonId button, ButtonEvent event) override;
    void render(RenderMode mode) override;

private:
    esp_err_t last_result_ = ESP_OK;
};

/**
 * @brief 设置页，提供单层菜单和大按键修改设置项
 */
class SettingsPage final : public Page {
public:
    PageId id() const override;
    const char* title() const override;
    uint32_t refresh_interval_ms() const override;
    bool supports_edit_mode() const override;
    bool is_overlay_active() const override;
    void on_edit_enter() override;
    void on_edit_exit() override;
    bool handle_button(ButtonId button, ButtonEvent event) override;
    void render(RenderMode mode) override;

    /**
     * @brief 从 NVS 读取设置页使用的 UI 配置
     */
    void load_config();

private:
    enum class Mode : uint8_t {
        View,
        Menu,
    };

    enum Item : uint8_t {
        Rotate180,
        Backlight,
        WifiBoot,
        ProtectBypass,
        CanBaudrate,
        CanTerm,
        ITEM_COUNT,
    };

    static constexpr uint8_t VISIBLE_ROWS = 4;

    /**
     * @brief 获取设置项名称
     * @param item 设置项索引
     * @return 静态名称字符串
     */
    const char* item_name(uint8_t item) const;

    /**
     * @brief 获取设置项当前值显示文本
     * @param item 设置项索引
     * @return 值显示字符串
     */
    const char* item_value(uint8_t item);

    /**
     * @brief 调整当前选中的设置项
     */
    void adjust_selected_item();

    Mode mode_ = Mode::View;
    uint8_t selected_ = 0;
    bool rotation_180_ = false;
    uint8_t backlight_level_ = DEFAULT_BACKLIGHT_LEVEL;
    char value_buf_[8] = {};
};

} // namespace SCREEN

#endif
