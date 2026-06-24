/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 设置页面声明
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#ifndef SCREEN_SETTINGS_PAGE_H
#define SCREEN_SETTINGS_PAGE_H

#include "config/display_config.h"
#include "core/page.h"

namespace SCREEN {

/** @brief 设置页面，提供固定容量菜单、详情弹窗和动作确认流程。 */
class SettingsPage final : public Page {
public:
    /** @brief 获取页面标识。 */
    PageId id() const override;
    /** @brief 获取页面标题。 */
    const char* title() const override;
    /** @brief 获取刷新周期，单位 ms。 */
    uint32_t refresh_interval_ms() const override;
    /** @brief 设置页支持编辑菜单。 */
    bool supports_edit_mode() const override;
    /** @brief 查询菜单或弹窗是否激活。 */
    bool is_overlay_active() const override;
    /** @brief 进入页面时刷新持久化显示配置。 */
    void on_enter() override;
    /** @brief 进入设置菜单。 */
    void on_edit_enter() override;
    /** @brief 退出设置菜单。 */
    void on_edit_exit() override;
    /** @brief 处理设置页按键事件。 */
    bool handle_button(ButtonId button, ButtonEvent event) override;
    /** @brief 绘制设置页面。 */
    void render(RenderMode mode) override;

private:
    /** @brief 页面交互模式。 */
    enum class Mode : uint8_t { View, Menu, Dialog };
    /** @brief 设置项交互类型。 */
    enum class ItemType : uint8_t { Adjustable, Detail, Action };
    /** @brief 当前支持的设置项标识。 */
    enum Item : uint8_t {
        Rotate180, Backlight, WebBoot, ProtectBypass, BlackboxSnapshot,
        EspNowPair, EspNowInfo, CanBaudrate, CanTerm, FirmwareInfo,
        FirmwareUpdate, BlackboxInfo, CalibrationInfo, ITEM_COUNT,
    };

    static constexpr uint8_t VISIBLE_ROWS = 3; // 菜单同时显示的行数

    /** @brief 从 NVS 读取显示配置。 */
    void load_config();
    /** @brief 获取设置项名称。 */
    const char* item_name(uint8_t item) const;
    /** @brief 获取设置项当前值。 */
    const char* item_value(uint8_t item);
    /** @brief 获取设置项交互类型。 */
    ItemType item_type(uint8_t item) const;
    /** @brief 激活当前选中项。 */
    void activate_selected_item();
    /** @brief 运行动作设置项。 */
    bool run_action_item(uint8_t item);
    /** @brief 构建当前弹窗文本。 */
    void build_dialog_content();
    /** @brief 绘制当前设置弹窗。 */
    void draw_dialog_overlay();
    /** @brief 修改当前可调设置项。 */
    void adjust_selected_item();

    Mode    mode_               = Mode::View; // 当前页面交互模式
    uint8_t selected_           = 0; // 当前选中的设置项索引
    bool    rotation_180_        = false; // 当前屏幕旋转状态
    uint8_t backlight_level_     = DEFAULT_BACKLIGHT_LEVEL; // 当前背光档位
    char    value_buf_[8]        = {}; // 设置值格式化缓冲区
    char    detail_lines_[4][28] = {}; // 弹窗四行文本缓冲区
    bool    update_confirm_      = false; // OTA 升级二次确认状态
};

} // namespace SCREEN

#endif // SCREEN_SETTINGS_PAGE_H
