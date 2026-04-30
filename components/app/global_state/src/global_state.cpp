#include "global_state.h"

static GlobalState global_state = {};

int16_t* current_register_raw=nullptr;
uint16_t* voltage_register_raw=nullptr;

GlobalState& get_global_state(){
    return global_state;
}

