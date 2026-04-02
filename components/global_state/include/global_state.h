#ifndef GLOBAL_STATE_H
#define GLOBAL_STATE_H
#include "protect.h"
#include <cstdint>
struct GlobalState{
    uint32_t voltage_uV; 
    int32_t current_nA;
    float NTC_temperature;
    float chip_temperature;
    uint32_t out_put_state; //4字节对齐
    protect_states_t protect_states;
}__attribute__((packed));

GlobalState& get_global_state();

#endif