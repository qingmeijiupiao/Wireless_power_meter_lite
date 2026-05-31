/*
 * @Description: 黑匣子全局状态快照协议与采样
 */
#include "blackbox_service.h"

namespace BlackboxService {
namespace {

auto& global_state_ref = get_global_state();

} // namespace

esp_err_t append_snapshot() {
    SnapshotV1 snapshot = {
        .version = SNAPSHOT_VERSION,
        .flags = global_state_ref.flags,
        .protect_states = global_state_ref.protect_states,
        .voltage_mV = global_state_ref.voltage_mV,
        .current_uA = global_state_ref.current_uA,
        .meter_mwh = global_state_ref.meter_uwh / 1000,
        .board_temperature = global_state_ref.board_temperature,
        .chip_temperature = global_state_ref.chip_temperature,
    };
    return Blackbox::append_typed(Blackbox::LogType::STRUCTURED,
                                  reinterpret_cast<uint8_t*>(&snapshot),
                                  sizeof(snapshot));
}

} // namespace BlackboxService
