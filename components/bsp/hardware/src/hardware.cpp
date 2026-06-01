/*
 * @Description: 
 * @Author: qingmeijiupiao
 * @version: 
 * @Date: 2026-04-25 00:50:00
 * @LastEditTime: 2026-04-29 00:34:50
 */
#include "hardware.h"
#include "adc.h"

adc_t hardware_adc(hardware_adc_channel);
uint8_t hardware_version = 255;
const hardware_config version_1={
    .TFT_SCL = GPIO_NUM_15,
    .TFT_SDA = GPIO_NUM_4,
    .TFT_RST = GPIO_NUM_2,
    .TFT_RS = GPIO_NUM_3,
    .TFT_CS = GPIO_NUM_0,
    .TFT_BLK = GPIO_NUM_23,
    .TFT_BLK_ACTIVE_STATE = false,
    .temperature_channel = ADC_CHANNEL_5,
    .CAN_TX = GPIO_NUM_18,
    .CAN_RX = GPIO_NUM_14,
    .CAN_RESISTOR_ENABLE = GPIO_NUM_16,
    .INAA226_SDA = GPIO_NUM_6,
    .INAA226_SCL = GPIO_NUM_7,  
    .INAA226_ALERT = GPIO_NUM_20,
    .OUTPUT_CTRL = GPIO_NUM_21,
    .MAIN_BUTTON = GPIO_NUM_17,
    .SIDE_BUTTON = GPIO_NUM_9,
};

#define TAG "hardware"
esp_err_t hardware_config_init(){
    esp_err_t ret = ESP_OK;
    ret = hardware_adc.init();
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "hardware version ADC init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int adc_value = 0;
    int sum = 0;
    int err_count = 0;
    for (int i = 0; i < 10; i++) {
        ret = hardware_adc.read_raw(adc_value);
        if(ret != ESP_OK){
            ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
            return ret;
        }
        if((abs(adc_value -(sum/i)) > 165) && (i > 0)){ // 与前一次的平均值差大于300mv，认为是异常值
            err_count++;
            i--;
            ESP_LOGW(TAG, "ADC value is invalid, err_count: %d", err_count);
            if(err_count > 5){
                ESP_LOGE(TAG, "ADC value is invalid, err_count: %d", err_count);
                return ESP_ERR_INVALID_STATE; // 5次以上异常，认为ADC是浮空异常
            }
            continue;
        }
        ESP_LOGD(TAG, "ADC value: %d", adc_value);
        sum += adc_value;
    }
    adc_value = sum / 10;

    hardware_version = ((uint8_t)(adc_value+165)/330)+1; //每330mv一个版本 ，版本号从0mV-版本1开始
    ESP_LOGI(TAG, "Hardware version: %d", hardware_version);
    
    return ESP_OK;
}

uint8_t get_hardware_version(){
    return hardware_version;
}

const hardware_config& get_hardware_config(){
    switch (get_hardware_version()){
    case 1:
        return version_1;
    default:
        ESP_LOGW(TAG, "Unknown hardware version: %d, use default config", get_hardware_version());
        return version_1;
    }
    return version_1;
}
