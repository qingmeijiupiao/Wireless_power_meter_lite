/*
 * @LastEditors: qingmeijiupiao
 * @Description: 温度传感器库
 * @Author: qingmeijiupiao
 * @Date: 2024-09-06 18:38:48
 */
#ifndef TEMPERATURESENSOR_HPP
#define TEMPERATURESENSOR_HPP
#include "driver/temperature_sensor.h"
#include "hal/temperature_sensor_periph.h"
#include "esp_err.h"
#include "esp_log.h"


// {
//     // Offset  reg_val  min  max  error  说明
//     {-2,        5,     50,  125,   3},  // 范围75: 50-125°C，误差±3°C
//     {-1,        7,     20,  100,   2},  // 范围80: 20-100°C，误差±2°C
//     { 0,       15,    -10,   80,   1},  // 范围90: -10-80°C，误差±1°C (默认范围)
//     { 1,       11,    -30,   50,   2},  // 范围60: -30-50°C，误差±2°C
//     { 2,       10,    -40,   20,   3},  // 范围40: -40-20°C，误差±3°C
// };


/**
 * @brief ESP芯片内置温度传感器类
 * 
 * 该类封装了ESP芯片内置温度传感器的功能，支持自动切换温度范围
 * 以提高测量精度。当温度接近当前范围的边界时，会自动切换到更合适的范围。
 */
class TemperatureSensor_t{
    public:
    /**
     * @brief 初始化温度传感器
     * 
     * 设置默认温度范围并启动传感器。默认使用范围90(-10°C到80°C)，
     * 这是最常用的温度范围，精度最高(±1°C)。
     */
    esp_err_t init() {
        uint8_t default_range_index = 2; // 默认使用范围90(-10°C到80°C)
        current_range_index = default_range_index;
        
        // 初始化默认范围的温度边界值
        absolute_max_temperature = temperature_sensor_attributes[0].range_max;
        absolute_min_temperature = temperature_sensor_attributes[TEMPERATURE_SENSOR_ATTR_RANGE_NUM-1].range_min;

        // 配置温度传感器参数
        tsens_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(
            temperature_sensor_attributes[default_range_index].range_min, 
            temperature_sensor_attributes[default_range_index].range_max
        );
        
        // 安装并启用温度传感器
        ESP_ERROR_CHECK(temperature_sensor_install(reinterpret_cast<temperature_sensor_config_t*>(&tsens_config), &tsens));
        ESP_ERROR_CHECK(temperature_sensor_enable(tsens));
        
        // 记录当前范围的边界值
        current_range_min = temperature_sensor_attributes[default_range_index].range_min;
        current_range_max = temperature_sensor_attributes[default_range_index].range_max;
        return ESP_OK;
    }
    /**
     * @brief 获取当前温度值
     * 
     * 读取当前温度，并根据需要自动切换温度范围以提高精度
     * 
     * @return float 当前温度值(摄氏度)
     */
    float getTemperature() {
        // 读取当前温度值
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(tsens, &temp_data));
        
        // 检查是否需要切换温度范围
        int8_t switch_result = checkswitchRange();
        if (switch_result != 0) {
            // 切换到更合适的温度范围
            switchRange(current_range_index + switch_result);
            // 重新读取温度值
            ESP_ERROR_CHECK(temperature_sensor_get_celsius(tsens, &temp_data));
        }
        
        return temp_data;
    }
    protected:
    /**
     * @brief 切换温度传感器范围
     * 
     * 当温度接近当前范围的边界时，切换到更合适的范围以提高测量精度
     * 
     * @param range_index 要切换到的范围索引
     */
    void switchRange(uint8_t range_index) {
        // 检查范围索引是否有效
        if (range_index >= TEMPERATURE_SENSOR_ATTR_RANGE_NUM) {
            ESP_LOGE("TemperatureSensor", "无效的温度范围索引: %d", range_index);
            return;
        }
        
        ESP_LOGD("TemperatureSensor", "切换温度范围: %d -> %d", current_range_index, range_index);
        
        // 先禁用并卸载当前传感器
        ESP_ERROR_CHECK(temperature_sensor_disable(tsens));
        ESP_ERROR_CHECK(temperature_sensor_uninstall(tsens));
        
        // 重新配置新的温度范围
        tsens_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(
            temperature_sensor_attributes[range_index].range_min, 
            temperature_sensor_attributes[range_index].range_max
        );
        
        // 重新安装并启用传感器
        ESP_ERROR_CHECK(temperature_sensor_install(reinterpret_cast<temperature_sensor_config_t*>(&tsens_config), &tsens));
        ESP_ERROR_CHECK(temperature_sensor_enable(tsens));
        
        // 更新当前范围信息
        current_range_min = temperature_sensor_attributes[range_index].range_min;
        current_range_max = temperature_sensor_attributes[range_index].range_max;
        current_range_index = range_index;
    }
    /**
     * @brief 检查是否需要切换温度范围
     * 
     * 当温度接近当前范围的边界时(阈值 ± range_threshold °C)，切换到更合适的范围
     * 
     * @return int8_t -1:需要切换到更低范围, 1:需要切换到更高范围, 0:不需要切换
     */
    int8_t checkswitchRange() {
        constexpr int range_threshold = 10; // 范围切换阈值(°C)
        
        // 检查是否需要切换到更低温度范围
        if (temp_data < current_range_min + range_threshold && current_range_index != 0) {
            ESP_LOGD("TemperatureSensor", "温度%.1f°C接近下限，建议切换到更低范围", temp_data);
            return -1; // 需要切换到lower range
        }
        
        // 检查是否需要切换到更高温度范围
        if (temp_data > current_range_max - range_threshold && current_range_index != TEMPERATURE_SENSOR_ATTR_RANGE_NUM - 1) {
            ESP_LOGD("TemperatureSensor", "温度%.1f°C接近上限，建议切换到更高范围", temp_data);
            return 1; // 需要切换到upper range
        }
        
        return 0; // 不需要切换范围
    }
    private:
        temperature_sensor_handle_t tsens;           // 温度传感器句柄
        int16_t current_range_min = 0;                // 当前温度范围最小值
        int16_t current_range_max = 0;                // 当前温度范围最大值
        uint8_t current_range_index = 0;             // 当前温度范围索引
        int16_t absolute_max_temperature;  // 传感器支持的最大温度(°C)
        int16_t absolute_min_temperature;  // 传感器支持的最小温度(°C)
        float temp_data = 0.0f;                      // 临时存储的温度数据
        temperature_sensor_config_t tsens_config;    // 温度传感器配置
};
#endif