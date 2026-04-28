/*
 * @Description: TMP235 温度传感器
 * @Author: qingmeijiupiao
 * @version: 2.0.0
 * @Date: 2026-04-20 00:48:01
 * @LastEditTime: 2026-04-29 01:22:58
 */
#include "TMP235.h"
#include "esp_log.h"
TMP235_t& TMP235_t::instance() {
    static TMP235_t inst;
    return inst;
}

esp_err_t TMP235_t::init(adc_channel_t channel) {
    adc = new adc_t(channel);
    if (adc == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    return adc->init();
}

int16_t TMP235_t::getTemperature() {
    if (adc == nullptr) {
        ESP_LOGE("TMP235", "adc not initialized");
        return 0;
    }
    int adc_value_mv = 0;
    adc->read_voltage_mV(adc_value_mv);
    int16_t raw_temp;
    if (adc_value_mv < 1500) {
        raw_temp = (adc_value_mv - 500) * 10;
    } else if (adc_value_mv <= 1752) {
        raw_temp = (adc_value_mv - 1500) * 1000 / 101 + 10000;
    } else {
        raw_temp = (adc_value_mv - 1753) * 1000 / 106 + 12500;
    }

    avg_sum -= avg_buf[avg_buf_idx];
    avg_buf[avg_buf_idx] = raw_temp;
    avg_sum += raw_temp;
    avg_buf_idx = (avg_buf_idx + 1) % AVG_BUF_SIZE;
    if (avg_buf_count < AVG_BUF_SIZE) {
        avg_buf_count++;
    }
    return avg_sum / avg_buf_count;
}
