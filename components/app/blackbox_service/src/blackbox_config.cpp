/*
 * @Description: 黑匣子周期快照 NVS 配置
 */
#include "blackbox_service_internal.h"

#include "HXC_NVS.h"
#include "blackbox_service.h"
#include "freertos/FreeRTOS.h"

namespace BlackboxService {
namespace Internal {
namespace {

HXC::NVS_DATA<uint32_t> snapshot_interval_nvs("bb_snap_s", DEFAULT_SNAPSHOT_INTERVAL_S);
portMUX_TYPE config_lock = portMUX_INITIALIZER_UNLOCKED;
uint32_t snapshot_interval_s = DEFAULT_SNAPSHOT_INTERVAL_S;

} // namespace

void load_config() {
    const uint32_t saved_interval_s = snapshot_interval_nvs.read();
    portENTER_CRITICAL(&config_lock);
    snapshot_interval_s = saved_interval_s;
    portEXIT_CRITICAL(&config_lock);
}

uint32_t read_snapshot_interval_s() {
    portENTER_CRITICAL(&config_lock);
    const uint32_t result = snapshot_interval_s;
    portEXIT_CRITICAL(&config_lock);
    return result;
}

esp_err_t write_snapshot_interval_s(uint32_t seconds) {
    const esp_err_t err = snapshot_interval_nvs.set(seconds);
    if (err != ESP_OK) {
        return err;
    }
    portENTER_CRITICAL(&config_lock);
    snapshot_interval_s = seconds;
    portEXIT_CRITICAL(&config_lock);
    return ESP_OK;
}

} // namespace Internal
} // namespace BlackboxService
