/*
 * @Description: 黑匣子全局状态快照协议与采样
 */
#include "blackbox_service.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace BlackboxService {
namespace {

auto& global_state_ref = get_global_state();
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
int64_t last_snapshot_ms = -static_cast<int64_t>(MIN_SNAPSHOT_INTERVAL_MS);

} // namespace

esp_err_t append_snapshot(bool force) {
    const int64_t now_ms = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&snapshot_lock);
    const int64_t previous_snapshot_ms = last_snapshot_ms;
    if (!force && now_ms - previous_snapshot_ms < MIN_SNAPSHOT_INTERVAL_MS) {
        portEXIT_CRITICAL(&snapshot_lock);
        return ESP_OK;
    }
    last_snapshot_ms = now_ms;
    portEXIT_CRITICAL(&snapshot_lock);

    SnapshotV1 snapshot = {
        .version = SNAPSHOT_VERSION,
        .flags = global_state_ref.flags,
        .protect_states = global_state_ref.protect_states,
        .voltage_mV = global_state_ref.voltage_mV,
        .current_uA = global_state_ref.current_uA,
        .meter_mwh = global_state_ref.meter_mwh,
        .board_temperature = global_state_ref.board_temperature,
        .chip_temperature = global_state_ref.chip_temperature,
    };
    const esp_err_t ret = Blackbox::append_typed(Blackbox::LogType::STRUCTURED,
                                                 reinterpret_cast<uint8_t*>(&snapshot),
                                                 sizeof(snapshot));
    if (ret != ESP_OK) {
        portENTER_CRITICAL(&snapshot_lock);
        if (last_snapshot_ms == now_ms) {
            last_snapshot_ms = previous_snapshot_ms;
        }
        portEXIT_CRITICAL(&snapshot_lock);
    }
    return ret;
}

} // namespace BlackboxService
