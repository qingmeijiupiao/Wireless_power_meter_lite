#ifndef PROTECT_H
#define PROTECT_H
#include <stdint.h>
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
    ProtectState_t voltage_protect_state : 2;
    ProtectState_t current_protect_state : 2;
    ProtectState_t reverse : 2;
    } states_bit; //1字节对齐
} __attribute__((packed));

void protect_task(void* pvParameters);

#endif