/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 功率输出控制模块实现，基于策略链架构
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-05-01 01:51:17
 */
#include "power_output.h"
#include "protect_policy.hpp"
#include "cooldown_policy.hpp"
#include "cpp_gpio_driver.hpp"
#include "global_state.h"
#include "protect.h"
#include "esp_log.h"
#include <array>

namespace PowerOutput {

static const char* TAG = "PowerOutput";

constexpr size_t MAX_POLICIES = 8;
constexpr size_t MAX_CALLBACKS = 8;

// =====================================================================
//  私有变量
// =====================================================================

static CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> _output_gpio;
static bool _initialized = false;

static std::array<OnOutputChangeCallback, MAX_CALLBACKS> _change_callbacks;
static size_t _callback_count = 0;

static std::array<OutputPolicy*, MAX_POLICIES> _policies;
static size_t _policy_count = 0;

static ProtectPolicy _protect_policy;
static CooldownPolicy _cooldown_policy(OUTPUT_ON_COOLDOWN_MS, OUTPUT_OFF_COOLDOWN_MS);

// =====================================================================
//  内部辅助函数
// =====================================================================

static void notify_change(bool new_state) {
    for (size_t i = 0; i < _callback_count; i++) {
        _change_callbacks[i](new_state);
    }
}

static void apply_state(bool state) {
    _output_gpio.set(state);
    notify_change(state);
}

static void notify_policies_applied(OutputOperation op, bool new_state) {
    for (size_t i = 0; i < _policy_count; i++) {
        _policies[i]->on_state_applied(op, new_state);
    }
}

static OutputResult check_policies(OutputOperation op) {
    bool current = get_state();
    for (size_t i = 0; i < _policy_count; i++) {
        OutputResult result = _policies[i]->check(op, current);
        if (result != OutputResult::OK) {
            return result;
        }
    }
    return OutputResult::OK;
}

// =====================================================================
//  模块生命周期 —— init / deinit
// =====================================================================

esp_err_t init(gpio_num_t output_gpio_num) {
    if (_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    // 初始化 GPIO，默认关闭输出
    ESP_ERROR_CHECK(_output_gpio.init(output_gpio_num));
    _output_gpio.set_on_change_callback([](bool value){
        auto& state = get_global_state();
        state.flags.bits.output_enabled = value;
    });
    _output_gpio.set(false);

    // 初始化策略链：保护策略 -> 冷却策略（按注册顺序依次检查）
    _cooldown_policy = CooldownPolicy(OUTPUT_ON_COOLDOWN_MS, OUTPUT_OFF_COOLDOWN_MS);
    _policy_count = 0;
    add_policy(&_protect_policy);
    add_policy(&_cooldown_policy);

    // 监听保护状态变更，保护触发时强制关闭输出
    add_on_protect_change_callback([](ProtectState_t last_state, ProtectState_t new_state) {
        if (new_state == PROTECT_STATE_PROTECT) {
            if (protect_should_block_output()) {
                ESP_LOGW(TAG, "protect triggered, force disable output");
                apply_state(false);
                notify_policies_applied(OutputOperation::OFF, false);
            } else {
                ESP_LOGW(TAG, "protect triggered but bypassed, output kept unchanged");
            }
        }
    });

    _initialized = true;
    ESP_LOGI(TAG, "initialized on GPIO %d, on_cooldown %lu ms, off_cooldown %lu ms", output_gpio_num, (unsigned long)OUTPUT_ON_COOLDOWN_MS, (unsigned long)OUTPUT_OFF_COOLDOWN_MS);
    return ESP_OK;
}

esp_err_t deinit() {
    if (!_initialized) {
        ESP_LOGW(TAG, "not initialized");
        return ESP_OK;
    }
    apply_state(false);
    _initialized = false;
    _callback_count = 0;
    _policy_count = 0;
    return ESP_OK;
}

// =====================================================================
//  输出操作 —— on / off / toggle
//  流程：初始化检查 -> 策略链检查 -> 执行 -> 通知策略 -> 日志
// =====================================================================

OutputResult on() {
    if (!_initialized) {
        ESP_LOGE(TAG, "on: not initialized");
        return OutputResult::FAIL_NOT_INIT;
    }
    OutputResult result = check_policies(OutputOperation::ON);
    if (result != OutputResult::OK) {
        return result;
    }
    apply_state(true);
    notify_policies_applied(OutputOperation::ON, true);
    ESP_LOGI(TAG, "output on");
    return OutputResult::OK;
}

OutputResult off() {
    if (!_initialized) {
        ESP_LOGE(TAG, "off: not initialized");
        return OutputResult::FAIL_NOT_INIT;
    }
    OutputResult result = check_policies(OutputOperation::OFF);
    if (result != OutputResult::OK) {
        return result;
    }
    apply_state(false);
    notify_policies_applied(OutputOperation::OFF, false);
    ESP_LOGI(TAG, "output off");
    return OutputResult::OK;
}

OutputResult toggle() {
    if (!_initialized) {
        ESP_LOGE(TAG, "toggle: not initialized");
        return OutputResult::FAIL_NOT_INIT;
    }
    OutputOperation op = get_state() ? OutputOperation::OFF : OutputOperation::ON;
    OutputResult result = check_policies(op);
    if (result != OutputResult::OK) {
        return result;
    }
    bool new_state = (op == OutputOperation::ON);
    apply_state(new_state);
    notify_policies_applied(op, new_state);
    ESP_LOGI(TAG, "output toggled to %s", new_state ? "ON" : "OFF");
    return OutputResult::OK;
}

// =====================================================================
//  查询与回调
// =====================================================================

bool get_state() {
    return _output_gpio.get();
}

void add_on_change_callback(OnOutputChangeCallback cb) {
    if (_callback_count < MAX_CALLBACKS) {
        _change_callbacks[_callback_count++] = cb;
    } else {
        ESP_LOGE(TAG, "callback list full, max %zu", MAX_CALLBACKS);
    }
}

void add_policy(OutputPolicy* policy) {
    if (_policy_count < MAX_POLICIES) {
        _policies[_policy_count++] = policy;
    } else {
        ESP_LOGE(TAG, "policy list full, max %zu", MAX_POLICIES);
    }
}

}
