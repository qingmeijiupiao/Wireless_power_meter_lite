/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 保护功能相关数据结构
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:39:46
 */
#ifndef PROTECT_H
#define PROTECT_H
#include <stdint.h>
#include <functional>
#include "esp_err.h"
enum ProtectState_t : uint8_t{
    PROTECT_STATE_NORMAL = 0,
    PROTECT_STATE_WARNING = 1,
    PROTECT_STATE_PROTECT = 2,
};

//大于阈值触发该状态
struct protect_threshold_t{
    float warning_threshold;
    float warning_recovery_threshold;
    float protect_threshold;
    float protect_recovery_threshold;
    uint32_t is_asc; //true: 大于阈值触发，false: 小于阈值触发 4字节对齐
} __attribute__((packed));

union protect_states_t{
    uint8_t protect_states_raw;
    struct {
    ProtectState_t temperature_protect_state : 2;
    ProtectState_t high_voltage_protect_state : 2;
    ProtectState_t low_voltage_protect_state : 2;
    ProtectState_t current_protect_state : 2;
    } states_bit; //1字节对齐
} __attribute__((packed));

struct protect_channel_info_t{
    const char* name;
    const char* unit;
    float now_value;
    ProtectState_t state;
    protect_threshold_t threshold;
};

void add_on_protect_change_callback(std::function<void(ProtectState_t last_state, ProtectState_t new_state)> cb);

bool have_protect();
bool protect_has_active_fault();
bool protect_should_block_output();
void protect_set_bypassed(bool bypassed, const char* source);
bool protect_is_bypassed();
uint8_t protect_get_channel_count();
bool protect_get_channel_info(uint8_t index, protect_channel_info_t* info);

esp_err_t protect_init();
bool protect_init_ok();
esp_err_t protect_deinit();

#endif
