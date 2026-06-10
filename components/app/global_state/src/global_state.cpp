#include "global_state.h"

#include "freertos/FreeRTOS.h"

static GlobalState global_state = {};
static portMUX_TYPE measurement_lock = portMUX_INITIALIZER_UNLOCKED;

GlobalState& get_global_state(){
    return global_state;
}

void update_global_measurement(const GlobalMeasurementSnapshot& snapshot) {
    portENTER_CRITICAL(&measurement_lock);
    global_state.voltage_mV = snapshot.voltage_mV;
    global_state.current_uA = snapshot.current_uA;
    global_state.current_register_raw = snapshot.current_register_raw;
    global_state.voltage_register_raw = snapshot.voltage_register_raw;
    portEXIT_CRITICAL(&measurement_lock);
}

GlobalMeasurementSnapshot get_global_measurement_snapshot() {
    portENTER_CRITICAL(&measurement_lock);
    const GlobalMeasurementSnapshot snapshot = {
        .voltage_mV = global_state.voltage_mV,
        .current_uA = global_state.current_uA,
        .current_register_raw = global_state.current_register_raw,
        .voltage_register_raw = global_state.voltage_register_raw,
    };
    portEXIT_CRITICAL(&measurement_lock);
    return snapshot;
}

