#ifndef ENERGY_METER_H
#define ENERGY_METER_H

#include <cstdint>

namespace EnergyMeter {

/**
 * @brief 共享计量会话快照。
 */
struct Snapshot {
    int64_t charge_uah;      /**< 相对当前基线的累计电量，单位 μAh。 */
    int64_t energy_uwh;      /**< 相对当前基线的累计能量，单位 μWh。 */
    uint64_t meter_time_ms;  /**< 当前计量会话持续时间，单位 ms。 */
};

/**
 * @brief 获取当前共享计量会话快照。
 * @return 相对计量值和会话持续时间。
 */
Snapshot snapshot();

/**
 * @brief 将当前 LP Core 累计值设置为新的共享计量基线。
 *
 * 该操作不会修改 LP Core 自启动以来持续累加的底层计数器。
 */
void reset();

} // namespace EnergyMeter

#endif
