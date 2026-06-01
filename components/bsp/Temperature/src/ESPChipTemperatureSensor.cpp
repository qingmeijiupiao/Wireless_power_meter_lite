/*
 * @Description: ESP芯片内置温度传感器
 * @Author: qingmeijiupiao
 * @version: 2.0.0
 * @Date: 2024-09-06 18:38:48
 * @LastEditTime: 2026-04-29 01:22:25
 */
#include "ESPChipTemperatureSensor.h"

ESPChipTemperatureSensor_t& ESPChipTemperatureSensor_t::instance() {
    static ESPChipTemperatureSensor_t inst;
    return inst;
}

esp_err_t ESPChipTemperatureSensor_t::init() {
    uint8_t default_range_index = 2;
    current_range_index = default_range_index;

    absolute_max_temperature = temperature_sensor_attributes[0].range_max;
    absolute_min_temperature = temperature_sensor_attributes[TEMPERATURE_SENSOR_ATTR_RANGE_NUM - 1].range_min;

    tsens_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(
        temperature_sensor_attributes[default_range_index].range_min,
        temperature_sensor_attributes[default_range_index].range_max
    );

    esp_err_t ret = temperature_sensor_install(reinterpret_cast<temperature_sensor_config_t*>(&tsens_config), &tsens);
    if (ret != ESP_OK) {
        ESP_LOGE("ESPChipTemperatureSensor", "sensor install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = temperature_sensor_enable(tsens);
    if (ret != ESP_OK) {
        ESP_LOGE("ESPChipTemperatureSensor", "sensor enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    current_range_min = temperature_sensor_attributes[default_range_index].range_min;
    current_range_max = temperature_sensor_attributes[default_range_index].range_max;
    return ESP_OK;
}

float ESPChipTemperatureSensor_t::getTemperature() {
    if (tsens == nullptr) {
        if (!fault_reported) {
            ESP_LOGE("ESPChipTemperatureSensor", "temperature sensor not initialized");
            fault_reported = true;
        }
        return 0;
    }
    esp_err_t ret = temperature_sensor_get_celsius(tsens, &temp_data);
    if (ret != ESP_OK) {
        if (!fault_reported) {
            ESP_LOGE("ESPChipTemperatureSensor", "sensor read failed: %s", esp_err_to_name(ret));
            fault_reported = true;
        }
        return temp_data;
    }
    if (fault_reported) {
        ESP_LOGI("ESPChipTemperatureSensor", "sensor reading recovered");
        fault_reported = false;
    }

    int8_t switch_result = checkswitchRange();
    if (switch_result != 0) {
        ret = switchRange(current_range_index + switch_result);
        if (ret != ESP_OK) {
            return temp_data;
        }
        ret = temperature_sensor_get_celsius(tsens, &temp_data);
        if (ret != ESP_OK) {
            ESP_LOGE("ESPChipTemperatureSensor", "sensor read after range switch failed: %s",
                     esp_err_to_name(ret));
        }
    }

    return temp_data;
}

esp_err_t ESPChipTemperatureSensor_t::switchRange(uint8_t range_index) {
    if (range_index >= TEMPERATURE_SENSOR_ATTR_RANGE_NUM) {
        ESP_LOGE("ESPChipTemperatureSensor", "无效的温度范围索引: %d", range_index);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD("ESPChipTemperatureSensor", "切换温度范围: %d -> %d", current_range_index, range_index);

    esp_err_t ret = temperature_sensor_disable(tsens);
    if (ret != ESP_OK) {
        ESP_LOGE("ESPChipTemperatureSensor", "sensor disable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = temperature_sensor_uninstall(tsens);
    if (ret != ESP_OK) {
        ESP_LOGE("ESPChipTemperatureSensor", "sensor uninstall failed: %s", esp_err_to_name(ret));
        return ret;
    }
    tsens = nullptr;

    tsens_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(
        temperature_sensor_attributes[range_index].range_min,
        temperature_sensor_attributes[range_index].range_max
    );

    ret = temperature_sensor_install(reinterpret_cast<temperature_sensor_config_t*>(&tsens_config), &tsens);
    if (ret != ESP_OK) {
        ESP_LOGE("ESPChipTemperatureSensor", "sensor reinstall failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = temperature_sensor_enable(tsens);
    if (ret != ESP_OK) {
        ESP_LOGE("ESPChipTemperatureSensor", "sensor re-enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    current_range_min = temperature_sensor_attributes[range_index].range_min;
    current_range_max = temperature_sensor_attributes[range_index].range_max;
    current_range_index = range_index;
    return ESP_OK;
}

int8_t ESPChipTemperatureSensor_t::checkswitchRange() {
    constexpr int range_threshold = 10;

    if (temp_data < current_range_min + range_threshold && current_range_index != 0) {
        ESP_LOGD("ESPChipTemperatureSensor", "温度%.1f°C接近下限，建议切换到更低范围", temp_data);
        return -1;
    }

    if (temp_data > current_range_max - range_threshold && current_range_index != TEMPERATURE_SENSOR_ATTR_RANGE_NUM - 1) {
        ESP_LOGD("ESPChipTemperatureSensor", "温度%.1f°C接近上限，建议切换到更高范围", temp_data);
        return 1;
    }

    return 0;
}
