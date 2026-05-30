#ifndef CAN_RESISTOR_H
#define CAN_RESISTOR_H

#include "driver/gpio.h"
#include "esp_err.h"

class CanResistor {
public:
    static CanResistor& instance();

    esp_err_t init(gpio_num_t gpio_num);
    esp_err_t set(bool enabled);
    esp_err_t toggle();
    bool get() const;

private:
    CanResistor() = default;
    CanResistor(const CanResistor&) = delete;
    CanResistor& operator=(const CanResistor&) = delete;

    bool initialized_ = false;
};

#endif
