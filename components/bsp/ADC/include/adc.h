/*
 * @Description:  硬件配置和不同硬件版本适配模块头文件
 * @Author: qingmeijiupiao
 * @version: 1.0.0
 * @Date: 2026-04-25 00:49:51
 * @LastEditTime: 2026-04-25 02:23:01
 */
#ifndef ADC_H
#define ADC_H
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

class adc_t{
public:
    adc_t(adc_channel_t _channel):adc_channel(_channel){};
    ~adc_t(){};
    esp_err_t init();
    esp_err_t read_raw(int& raw);
    esp_err_t read_voltage_mV(int& voltage);
private:
    adc_channel_t adc_channel;
    adc_cali_handle_t cali_handle;
    static adc_oneshot_unit_handle_t adc1_unit_handle;
};

#endif
