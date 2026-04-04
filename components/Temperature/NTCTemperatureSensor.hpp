#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h" // 包含校准方案特定函数
#include "ntc_table.h"
#include "esp_err.h"
#include "Interp.hpp"

namespace NTC {
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12,    // 必须与通道配置的衰减一致
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 必须与通道配置的位宽一致
    };
    std::vector<std::pair<int16_t, int16_t>> points;
    NonEquidistantInterp<int16_t, int16_t>* interp_ntc = nullptr;
    adc_channel_t _channel=ADC_CHANNEL_0;
    
    esp_err_t init(adc_channel_t channel,adc_unit_t unit_id=ADC_UNIT_1){
        _channel = channel;
        init_config1.unit_id = unit_id;
        cali_config.unit_id = unit_id;
        cali_config.chan = channel;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, channel, &config));
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));
        points.reserve(NTC_TABLE_SIZE);
        for (int i = 0; i < NTC_TABLE_SIZE; i++) {
            points.emplace_back(ntc_table[i][0], ntc_table[i][1]);
        }
        interp_ntc = new NonEquidistantInterp<int16_t, int16_t>(points);
        if(interp_ntc == nullptr){
            return ESP_ERR_NO_MEM;
        }
        return ESP_OK;
    };
    
    /**
     * @brief :  获取NTC温度
     * @return  {int16_t} 温度值，单位为0.01摄氏度，例如返回2534表示25.34摄氏度
     */
    int16_t getTemperature() {
        static int16_t filtered_temp = 0;
        static bool first = true;
        constexpr float alpha = 0.1f;   // 滤波系数，越小越平滑，越大响应越快

        int adc_value = 0;
        int adc_value_mv = 0;
        adc_oneshot_read(adc1_handle, _channel, &adc_value);
        adc_cali_raw_to_voltage(cali_handle, adc_value, &adc_value_mv);
        int16_t raw_temp = interp_ntc->interpolate(adc_value_mv);

        if (first) {
            filtered_temp = raw_temp;
            first = false;
        } else {
            // 一阶低通滤波：output = α * new + (1-α) * old
            filtered_temp = static_cast<int16_t>(alpha * raw_temp + (1.0f - alpha) * filtered_temp);
        }
        return filtered_temp;
    }
}