#ifndef CPP_GPIO_DRIVER_HPP
#define CPP_GPIO_DRIVER_HPP
#include <driver/gpio.h>

enum class GpioMode {
    INPUT,
    OUTPUT,
    INPUT_PULLUP,
    INPUT_PULLDOWN
};

template <gpio_num_t GPIO, GpioMode Mode>
class CppGpioDriver {
public:
    CppGpioDriver() {
        io_conf.pin_bit_mask = (1ULL << GPIO);
        if constexpr (Mode == GpioMode::INPUT) {
            io_conf.mode = GPIO_MODE_INPUT;
        } else if constexpr (Mode == GpioMode::OUTPUT) {
            io_conf.mode = GPIO_MODE_OUTPUT;
        } else if constexpr (Mode == GpioMode::INPUT_PULLUP) {
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        } else if constexpr (Mode == GpioMode::INPUT_PULLDOWN) {
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
    }
    esp_err_t init(){
        return gpio_config(&io_conf);
    }

    esp_err_t set(bool value) {
        if constexpr (Mode == GpioMode::OUTPUT) {
            gpio_set_level(GPIO, value);
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    bool get() const {
        return gpio_get_level(GPIO);
    }
private:
    gpio_config_t io_conf = {};
};
#endif // CPP_GPIO_DRIVER_HPP