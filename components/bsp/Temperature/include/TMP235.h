/*
 * @Description: TMP235 温度传感器
 * @Author: qingmeijiupiao
 * @version: 2.0.0
 * @Date: 2026-04-20 00:48:01
 * @LastEditTime: 2026-04-26 00:43:50
 */
#ifndef TMP235_H
#define TMP235_H

#include "adc.h"
#include "esp_err.h"

class TMP235_t {
public:
    static TMP235_t& instance();

    TMP235_t(const TMP235_t&) = delete;
    TMP235_t& operator=(const TMP235_t&) = delete;

    esp_err_t init(adc_channel_t channel);

    /**
     * @brief :  获取TMP235温度
     * @return  {int16_t} 温度值，单位为0.01摄氏度，例如返回2534表示25.34摄氏度
     *          TMP235分段线性: V<1500: T=(V-500)/10, 1500<=V<=1752.5: T=(V-1500)/10.1+100, V>1752.5: T=(V-1752.5)/10.6+125
     */
    int16_t getTemperature();

private:
    TMP235_t() = default;

    adc_t* adc = nullptr;
    static constexpr size_t AVG_BUF_SIZE = 64;
    int16_t avg_buf[AVG_BUF_SIZE] = {};
    size_t avg_buf_idx = 0;
    size_t avg_buf_count = 0;
    int32_t avg_sum = 0;
};

#endif
