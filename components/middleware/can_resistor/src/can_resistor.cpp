#include "can_resistor.h"

#include "HXC_NVS.h"
#include "cpp_gpio_driver.hpp"
#include "esp_log.h"
#include "global_state.h"

namespace {

constexpr const char* TAG = "CanResistor";

CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> resistor_gpio;
HXC::NVS_DATA<uint8_t> saved_state("can_term", 0);

void update_global_state(bool enabled) {
    get_global_state().flags.bits.can_resistor_enabled = enabled;
}

} // namespace

CanResistor& CanResistor::instance() {
    static CanResistor controller;
    return controller;
}

esp_err_t CanResistor::init(gpio_num_t gpio_num) {
    if (initialized_) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    esp_err_t err = resistor_gpio.init(gpio_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init failed on pin %d: %s", gpio_num, esp_err_to_name(err));
        return err;
    }

    resistor_gpio.set_on_change_callback(update_global_state);
    err = resistor_gpio.set(saved_state.read() != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to restore saved state: %s", esp_err_to_name(err));
        return err;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "initialized on GPIO %d, state %s", gpio_num, get() ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t CanResistor::set(bool enabled) {
    if (!initialized_) {
        ESP_LOGW(TAG, "set ignored before initialization");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = resistor_gpio.set(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO write failed: %s", esp_err_to_name(err));
        return err;
    }

    saved_state = enabled ? 1 : 0;
    return ESP_OK;
}

esp_err_t CanResistor::toggle() {
    return set(!get());
}

bool CanResistor::get() const {
    return resistor_gpio.get();
}
