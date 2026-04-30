/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 功率输出控制模块，基于策略链架构实现可扩展的开关条件检查
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-04-30 12:55:16
 */
#ifndef POWER_OUTPUT_H
#define POWER_OUTPUT_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <functional>

namespace PowerOutput {

/** 输出操作结果枚举 */
enum class OutputResult : uint8_t {
    OK = 0,                    /**< 操作成功 */
    FAIL_NOT_INIT,             /**< 模块未初始化 */
    FAIL_PROTECT_ACTIVE,       /**< 保护状态激活，阻止开启 */
    FAIL_COOLDOWN_ACTIVE,      /**< 冷却时间未到，阻止操作 */
};

/** 输出操作类型枚举 */
enum class OutputOperation : uint8_t {
    ON,                        /**< 开启操作 */
    OFF,                       /**< 关闭操作 */
};

/**
 * @brief 输出策略基类，所有开关条件检查策略的接口
 * @note  继承此类实现自定义策略，通过 add_policy() 注册到策略链中，
 *        操作执行前依次调用 check()，全部通过才执行；
 *        操作完成后调用 on_state_applied() 通知策略更新内部状态
 */
class OutputPolicy {
public:
    virtual ~OutputPolicy() = default;

    /**
     * @brief  检查当前操作是否允许执行
     * @param  op 当前请求的操作类型
     * @param  current_state 当前输出状态 (true=ON, false=OFF)
     * @return OK 允许执行，其他值阻止执行并返回原因
     */
    virtual OutputResult check(OutputOperation op, bool current_state) = 0;

    /**
     * @brief  操作已执行后的通知回调，用于策略更新内部状态
     * @param  op 已执行的操作类型
     * @param  new_state 执行后的输出状态
     */
    virtual void on_state_applied(OutputOperation op, bool new_state) = 0;
};

using OnOutputChangeCallback = std::function<void(bool new_state)>;

/**
 * @brief  初始化功率输出模块
 * @param  output_gpio 输出控制 GPIO 引脚号
 * @return ESP_OK 成功，其他值失败
 * @note   内部自动注册 ProtectPolicy 和 CooldownPolicy，
 *         并监听保护状态变更，保护触发时强制关闭输出
 */
esp_err_t init(gpio_num_t output_gpio);

/**
 * @brief  反初始化模块，关闭输出并清理资源
 * @return ESP_OK 成功
 */
esp_err_t deinit();

/**
 * @brief  开启输出
 * @return OK 成功，FAIL_NOT_INIT 未初始化，FAIL_PROTECT_ACTIVE 保护激活，FAIL_COOLDOWN_ACTIVE 冷却中
 */
OutputResult on();

/**
 * @brief  关闭输出
 * @return OK 成功，FAIL_NOT_INIT 未初始化，FAIL_COOLDOWN_ACTIVE 冷却中
 */
OutputResult off();

/**
 * @brief  切换输出状态（开->关，关->开）
 * @return OK 成功，FAIL_NOT_INIT 未初始化，FAIL_PROTECT_ACTIVE 保护激活（切换到ON时），FAIL_COOLDOWN_ACTIVE 冷却中
 */
OutputResult toggle();

/**
 * @brief  获取当前输出状态
 * @return true 输出开启，false 输出关闭
 */
bool get_state();

/**
 * @brief  注册输出状态变更回调
 * @param  cb 回调函数，参数为新的输出状态
 */
void add_on_change_callback(OnOutputChangeCallback cb);

/**
 * @brief  注册自定义策略到策略链末尾
 * @param  policy 策略对象指针（调用方需保证对象生命周期）
 * @note   策略按注册顺序依次检查，任一策略返回非 OK 即阻止操作
 */
void add_policy(OutputPolicy* policy);

}

#endif
