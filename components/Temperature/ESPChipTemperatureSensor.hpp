/*
 * @LastEditors: qingmeijiupiao
 * @Description: 温度传感器库
 * @Author: qingmeijiupiao
 * @Date: 2024-09-06 18:38:48
 */
#ifndef TEMPERATURESENSOR_HPP
#define TEMPERATURESENSOR_HPP
#include "driver/temp_sensor.h"

//温度传感器
class TemperatureSensor_t{
    public:
    void init() {
        temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
        temp_sensor_set_config(temp_sensor);          //设定配置
        temp_sensor_start();                          //传感器启用
    }
    float getTemperature() {
        temp_sensor_read_celsius(&temp_data);    //温度数值存放
        return temp_data;
    }
    private:
        static float temp_data;
};
float TemperatureSensor_t::temp_data = 0;
#endif