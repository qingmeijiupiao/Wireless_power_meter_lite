#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H
#include "protect.h"
#include <cstdint>
union GlobalState_bit{
    uint32_t raw;
    struct {
        uint32_t out_put_state      : 1;  // 输出状态
        uint32_t can_resistor_state : 1;  // CAN电阻状态
        uint32_t protect_bypassed   : 1;  // 保护旁路状态，0=保护生效，1=仅检测不阻断输出
        uint32_t reverse : 29;
    } state_bit; // 需要保证所有字段的默认值为0
} __attribute__((packed)); //4字节对齐
static_assert(sizeof(GlobalState_bit) == 4, "GlobalState_bit size must be 4 bytes");

struct GlobalState{
    uint16_t voltage_mV; 
    int32_t current_uA;
    int16_t board_temperature;  //单位为0.01摄氏度
    int16_t chip_temperature;   //单位为0.01摄氏度
    protect_states_t protect_states;
    GlobalState_bit global_state_bits;
}__attribute__((packed));

GlobalState& get_global_state();

extern int16_t* current_register_raw;
extern uint16_t* voltage_register_raw;

#endif
