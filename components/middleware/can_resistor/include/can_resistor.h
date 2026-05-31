#ifndef CAN_RESISTOR_H
#define CAN_RESISTOR_H

#include "driver/gpio.h"
#include "esp_err.h"

class CanResistor {
public:
    /**
     * @brief 获取 CAN 终端电阻控制器单例。
     * @return 控制器引用。
     */
    static CanResistor& instance();

    /**
     * @brief 初始化终端电阻 GPIO，并恢复 NVS 中保存的状态。
     * @param gpio_num 终端电阻使能 GPIO。
     * @return ESP_OK 成功，其他值表示 GPIO 初始化或状态恢复失败。
     */
    esp_err_t init(gpio_num_t gpio_num);

    /**
     * @brief 设置终端电阻状态并写入 NVS。
     * @param enabled true 表示接入终端电阻。
     * @return ESP_OK 成功，未初始化时返回 ESP_ERR_INVALID_STATE。
     */
    esp_err_t set(bool enabled);

    /**
     * @brief 切换终端电阻状态并写入 NVS。
     * @return ESP_OK 成功，未初始化时返回 ESP_ERR_INVALID_STATE。
     */
    esp_err_t toggle();

    /**
     * @brief 获取当前终端电阻 GPIO 状态。
     * @return true 表示终端电阻已接入。
     */
    bool get() const;

private:
    CanResistor() = default;
    CanResistor(const CanResistor&) = delete;
    CanResistor& operator=(const CanResistor&) = delete;

    bool initialized_ = false;
};

#endif
