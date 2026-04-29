#include "power_output.h"
#include "cpp_gpio_driver.hpp"
#include "global_state.h"
#include "protect.h"
#include "esp_log.h"
#include <vector>

namespace PowerOutput {

static const char* TAG = "PowerOutput";

static CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> output_gpio;
static bool _initialized = false;

static std::vector<OnOutputChangeCallback> _change_callbacks;

static void notify_change(bool new_state) {
    for (auto& cb : _change_callbacks) {
        cb(new_state);
    }
}

static void apply_state(bool state) {
    output_gpio.set(state);
    auto& gs = get_global_state();
    gs.global_state_bits.state_bit.out_put_state = state;
    notify_change(state);
}

esp_err_t init(gpio_num_t output_gpio_num) {
    if (_initialized) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }
    ESP_ERROR_CHECK(output_gpio.init(output_gpio_num));
    output_gpio.set(false);
    get_global_state().global_state_bits.state_bit.out_put_state = false;
    _initialized = true;

    add_on_protect_change_callback([](ProtectState_t last_state, ProtectState_t new_state) {
        if (new_state == PROTECT_STATE_PROTECT) {
            ESP_LOGW(TAG, "protect triggered, force disable output");
            apply_state(false);
        }
    });

    ESP_LOGI(TAG, "initialized on GPIO %d", output_gpio_num);
    return ESP_OK;
}

esp_err_t deinit() {
    if (!_initialized) {
        ESP_LOGW(TAG, "not initialized");
        return ESP_OK;
    }
    output_gpio.set(false);
    get_global_state().global_state_bits.state_bit.out_put_state = false;
    _initialized = false;
    _change_callbacks.clear();
    return ESP_OK;
}

OutputResult on() {
    if (!_initialized) {
        ESP_LOGE(TAG, "on: not initialized");
        return OutputResult::FAIL_NOT_INIT;
    }
    if (have_protect()) {
        ESP_LOGW(TAG, "on: protect active, cannot turn on output");
        return OutputResult::FAIL_PROTECT_ACTIVE;
    }
    apply_state(true);
    ESP_LOGI(TAG, "output on");
    return OutputResult::OK;
}

OutputResult off() {
    if (!_initialized) {
        ESP_LOGE(TAG, "off: not initialized");
        return OutputResult::FAIL_NOT_INIT;
    }
    apply_state(false);
    ESP_LOGI(TAG, "output off");
    return OutputResult::OK;
}

OutputResult change() {
    if (!_initialized) {
        ESP_LOGE(TAG, "change: not initialized");
        return OutputResult::FAIL_NOT_INIT;
    }
    if (have_protect()) {
        ESP_LOGW(TAG, "change: protect active, cannot change output on");
        return OutputResult::FAIL_PROTECT_ACTIVE;
    }
    bool new_state = !get_state();
    apply_state(new_state);
    ESP_LOGI(TAG, "output changed to %s", new_state ? "ON" : "OFF");
    return OutputResult::OK;
}

bool get_state() {
    return get_global_state().global_state_bits.state_bit.out_put_state;
}

bool is_protect_active() {
    return have_protect();
}

void add_on_change_callback(OnOutputChangeCallback cb) {
    _change_callbacks.push_back(cb);
}

}
