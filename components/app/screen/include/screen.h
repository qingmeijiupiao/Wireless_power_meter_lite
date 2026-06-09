/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 屏幕 UI 应用组件公开接口，提供屏幕任务入口和跨任务按键事件投递接口
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30
 */
#ifndef SCREEN_H
#define SCREEN_H

#include <stdint.h>

#include "Button.h"
#include "esp_err.h"

namespace SCREEN {

/** Default startup logo display duration in milliseconds. */
static constexpr uint32_t DEFAULT_START_LOGO_DURATION_MS = 2000;
/** Maximum startup logo display duration in milliseconds. */
static constexpr uint32_t MAX_START_LOGO_DURATION_MS = 10000;

/**
 * @brief UI 层识别的物理按键
 */
enum class ButtonId : uint8_t {
    Main,  /**< 正面主按键 */
    Side,  /**< 侧边功能按键 */
};

/**
 * @brief 屏幕任务入口
 *
 * 初始化 ST7735 屏幕、UI 管理器和显示配置，随后持续处理 UI 事件并刷新当前页面。
 * @param arg FreeRTOS 任务参数，当前未使用
 */
void screen_task(void* arg);

/** @brief Bind application button routes and initialize their GPIO inputs. */
esp_err_t init_buttons();

/**
 * @brief Get the persisted startup logo display duration.
 * @return Duration in milliseconds. Zero disables the startup logo.
 */
uint32_t get_start_logo_duration_ms();

/**
 * @brief Persist the startup logo display duration.
 * @param duration_ms Duration in milliseconds. Zero disables the startup logo.
 */
esp_err_t set_start_logo_duration_ms(uint32_t duration_ms);

/**
 * @brief 向屏幕任务投递按键事件
 *
 * 按键回调运行在 Button 组件任务中，不能直接修改 UI 状态；该接口只负责将事件写入
 * UIManager 队列，由 screen_task 在线程内统一消费。
 * @param button 按键 ID
 * @param event 按键事件
 * @return true 表示事件已写入队列
 */
bool post_button_event(ButtonId button, ButtonEvent event);

} // namespace SCREEN

#endif
