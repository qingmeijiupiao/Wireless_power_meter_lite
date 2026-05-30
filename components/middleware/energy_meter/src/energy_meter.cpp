#include "energy_meter.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "global_state.h"

namespace EnergyMeter {
namespace {

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
int32_t charge_uah_baseline = 0;
int32_t energy_uwh_baseline = 0;
int64_t meter_start_us = 0;

} // namespace

Snapshot snapshot() {
    auto& state = get_global_state();
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&lock);
    Snapshot result = {
        .charge_uah = static_cast<int64_t>(state.meter_uah) - charge_uah_baseline,
        .energy_uwh = static_cast<int64_t>(state.meter_uwh) - energy_uwh_baseline,
        .meter_time_ms = static_cast<uint64_t>((now_us - meter_start_us) / 1000),
    };
    portEXIT_CRITICAL(&lock);
    return result;
}

void reset() {
    auto& state = get_global_state();
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&lock);
    charge_uah_baseline = state.meter_uah;
    energy_uwh_baseline = state.meter_uwh;
    meter_start_us = now_us;
    portEXIT_CRITICAL(&lock);
}

} // namespace EnergyMeter
