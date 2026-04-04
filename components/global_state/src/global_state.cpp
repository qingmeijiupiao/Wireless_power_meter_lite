#include "global_state.h"

static GlobalState global_state = {
    .voltage_mV = 0,
    .current_nA = 0,
    .protect_states = {
        .states_bit = {
            .temperature_protect_state = PROTECT_STATE_NORMAL,
            .voltage_protect_state = PROTECT_STATE_NORMAL,
            .current_protect_state = PROTECT_STATE_NORMAL,
        },
    },
};

GlobalState& get_global_state(){
    return global_state;
}
