#ifndef POWER_OUTPUT_H
#define POWER_OUTPUT_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <functional>

namespace PowerOutput {

enum class OutputResult : uint8_t {
    OK = 0,
    FAIL_PROTECT_ACTIVE,
    FAIL_NOT_INIT,
};

using OnOutputChangeCallback = std::function<void(bool new_state)>;

esp_err_t init(gpio_num_t output_gpio);
esp_err_t deinit();

OutputResult on();
OutputResult off();
OutputResult change();

bool get_state();
bool is_protect_active();

void add_on_change_callback(OnOutputChangeCallback cb);

}

#endif
