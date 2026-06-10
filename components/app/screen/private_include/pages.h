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
#include "curve_history.h"
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

/**
 * @brief 实时曲线页，支持指标、时间跨度切换和稳定自动量程。
 */
class CurvePage final : public Page {
public:
    /**
     * @brief 获取曲线页 ID
     * @return 曲线页 ID
     */
    PageId id() const override;

    /**
     * @brief 获取曲线页标题
     * @return 静态标题字符串
     */
    const char* title() const override;

    /**
     * @brief 获取曲线页刷新周期
     * @return 刷新周期，单位 ms
     */
    uint32_t refresh_interval_ms() const override;

    /**
     * @brief 声明曲线页支持参数编辑模式
     * @return 始终返回 true
     */
    bool supports_edit_mode() const override;

    /**
     * @brief 判断曲线页是否处于参数编辑状态
     * @return true 表示需要显示全局编辑提示
     */
    bool is_overlay_active() const override;

    /**
     * @brief 进入曲线参数编辑状态
     */
    void on_edit_enter() override;

    /**
     * @brief 退出曲线参数编辑状态
     */
    void on_edit_exit() override;

    /**
     * @brief 处理曲线页按键事件
     * @param button 按键 ID
     * @param event 按键事件
     * @return true 表示事件已由曲线页消费
     */
    bool handle_button(ButtonId button, ButtonEvent event) override;

    /**
     * @brief 绘制曲线页面
     * @param mode 页面渲染模式
     */
    void render(RenderMode mode) override;

private:
    enum class DisplayMode : uint8_t {
        Voltage,
        Current,
        Power,
        All,
        Count,
    };

    enum class EditItem : uint8_t {
        Display,
        TimeWindow,
        Count,
    };

    struct AutoRange {
        float minimum = 0.0f;
        float maximum = 1.0f;
        uint32_t shrink_candidate_ms = 0;
        bool initialized = false;
    };

    /**
     * @brief 获取当前时间跨度，单位 ms
     * @return 当前时间跨度
     */
    uint32_t window_ms() const;

    /**
     * @brief 获取显示模式短文本
     * @return 显示模式短文本
     */
    const char* display_mode_text() const;

    /**
     * @brief 获取时间跨度短文本
     * @return 时间跨度短文本
     */
    const char* window_text() const;

    /**
     * @brief 更新指定指标的自动量程
     * @param metric 指标类型
     * @param buckets 当前指标的像素桶
     * @param bucket_count 像素桶数量
     * @param now_ms 当前系统时间，单位 ms
     */
    void update_auto_range(CurveMetric metric, const CurveBucket* buckets,
                           size_t bucket_count, uint32_t now_ms);

    /**
     * @brief 绘制单指标曲线模式
     * @param metric 指标类型
     * @param color 曲线颜色
     */
    void draw_single_metric(CurveMetric metric, ST7735::color_t color);

    /**
     * @brief 绘制三指标叠加模式
     */
    void draw_all_metrics();

    /**
     * @brief 绘制曲线区域网格
     * @param x 左上角 X 坐标
     * @param y 左上角 Y 坐标
     * @param width 网格宽度
     * @param height 网格高度
     */
    void draw_grid(uint16_t x, uint16_t y, uint16_t width, uint16_t height) const;

    /**
     * @brief 绘制单组像素桶曲线
     * @param buckets 像素桶数组
     * @param bucket_count 像素桶数量
     * @param range 使用的自动量程
     * @param x 曲线区域左上角 X 坐标
     * @param y 曲线区域左上角 Y 坐标
     * @param height 曲线区域高度
     * @param color 曲线颜色
     */
    void draw_bucket_curve(const CurveBucket* buckets, size_t bucket_count,
                           const AutoRange& range, uint16_t x, uint16_t y,
                           uint16_t height, ST7735::color_t color) const;

    DisplayMode display_mode_ = DisplayMode::Voltage;
    EditItem edit_item_ = EditItem::Display;
    uint8_t window_index_ = 1;
    bool editing_ = false;
    AutoRange ranges_[static_cast<uint8_t>(CurveMetric::Count)] = {};
    CurveBucket buckets_[ST7735::WIDTH] = {};
};

/**
 * @brief 无线页，显示 STA、AP 配网和 ESP-NOW-only 状态，并支持长按进入 AP 配网
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
        Dialog,
    };

    enum class ItemType : uint8_t {
        Adjustable,
        Detail,
        Action,
    };

    enum Item : uint8_t {
        Rotate180,
        Backlight,
        WebBoot,
        ProtectBypass,
        BlackboxSnapshot,
        EspNowPair,
        EspNowInfo,
        CanBaudrate,
        CanTerm,
        FirmwareInfo,
        FirmwareUpdate,
        BlackboxInfo,
        CalibrationInfo,
        ITEM_COUNT,
    };

    static constexpr uint8_t VISIBLE_ROWS = 3;

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
     * @brief 获取设置项交互类型
     * @param item 设置项索引
     * @return 可调、详情或动作按钮
     */
    ItemType item_type(uint8_t item) const;

    /**
     * @brief 激活当前选中项
     */
    void activate_selected_item();

    /**
     * @brief 运行动作类设置项
     * @return true 表示需要打开弹窗显示动作状态
     */
    bool run_action_item(uint8_t item);

    /**
     * @brief 刷新当前弹窗内容，详情和动作弹窗均可复用
     */
    void build_dialog_content();

    /**
     * @brief 绘制当前设置项弹窗
     */
    void draw_dialog_overlay();

    /**
     * @brief 调整当前选中的设置项
     */
    void adjust_selected_item();

    Mode mode_ = Mode::View;
    uint8_t selected_ = 0;
    bool rotation_180_ = false;
    uint8_t backlight_level_ = DEFAULT_BACKLIGHT_LEVEL;
    char value_buf_[8] = {};
    char detail_lines_[4][28] = {};
    bool update_confirm_ = false;
};

} // namespace SCREEN

#endif
