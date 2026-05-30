#ifndef ENERGY_METER_H
#define ENERGY_METER_H

#include <cstdint>

namespace EnergyMeter {

struct Snapshot {
    int64_t charge_uah;
    int64_t energy_uwh;
    uint64_t meter_time_ms;
};

Snapshot snapshot();
void reset();

} // namespace EnergyMeter

#endif
