/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 该组件用于同步各种全局变量，包括保护状态、电压、电流、电能、温度等
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:39:18
 */
#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H
#include "protect.h"
#include <cstdint>
union GlobalStateFlags {
    uint32_t raw;
    struct {
        uint32_t output_enabled           : 1;
        uint32_t can_resistor_enabled     : 1;
        uint32_t protect_bypassed         : 1;
        uint32_t protect_initialized      : 1;
        uint32_t lp_core_running          : 1;
        uint32_t lp_ina226_initialized    : 1;
        uint32_t lp_i2c_error             : 1;
        uint32_t lp_ina226_read_timeout   : 1;
        uint32_t wifi_service_initialized : 1;
        uint32_t wifi_enabled             : 1;
        uint32_t wifi_sta_connected       : 1;
        uint32_t wifi_ap_mode             : 1;
        uint32_t wifi_has_saved_sta       : 1;
        uint32_t wifi_web_enabled_on_boot : 1;
        uint32_t web_backend_running      : 1;
        uint32_t screen_initialized       : 1;
        uint32_t blackbox_enabled         : 1;
        uint32_t reserved                 : 15;
    } bits;
};
static_assert(sizeof(GlobalStateFlags) == 4, "GlobalStateFlags size must be 4 bytes");

struct GlobalState{
    GlobalStateFlags flags;
    protect_states_t protect_states;
    uint16_t voltage_mV; 
    int32_t current_uA;
    float meter_mah;
    float meter_mwh;
    int16_t board_temperature;  //单位为0.01摄氏度
    int16_t chip_temperature;   //单位为0.01摄氏度
    int16_t current_register_raw;
    uint16_t voltage_register_raw;
};
static_assert(sizeof(GlobalState) == 28, "GlobalState size mismatch");

GlobalState& get_global_state();

#endif
