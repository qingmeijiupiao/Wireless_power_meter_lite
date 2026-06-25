#include "global_state.h"

#include "freertos/FreeRTOS.h"

static GlobalState  global_state      = {};
static portMUX_TYPE global_state_lock = portMUX_INITIALIZER_UNLOCKED;

namespace global_state_detail {

GlobalState& unsafe_ref() {
    return global_state;
}

void lock() {
    portENTER_CRITICAL(&global_state_lock);
}

void unlock() {
    portEXIT_CRITICAL(&global_state_lock);
}

} // namespace global_state_detail

const GlobalState get_global_state() {
    global_state_detail::lock();
    const GlobalState snapshot = global_state;
    global_state_detail::unlock();
    return snapshot;
}
