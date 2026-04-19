#include "global_state.h"

static GlobalState global_state = {};

GlobalState& get_global_state(){
    return global_state;
}
