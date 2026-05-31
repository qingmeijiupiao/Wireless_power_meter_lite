/*
 * @Description: 黑匣子事件消费与周期快照后台任务
 */
#include "blackbox_service_internal.h"

#include "blackbox_service.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace BlackboxService {
namespace Internal {
namespace {

constexpr TickType_t WORKER_POLL_TICKS = pdMS_TO_TICKS(20);

void blackbox_service_task(void*) {
    int64_t last_periodic_snapshot_ms = esp_timer_get_time() / 1000;
    while (true) {
        LogEvent event;
        while (pop_log_event(&event)) {
            if (event.include_text) {
                append_event("%s", event.text);
            } else {
                append_snapshot();
            }
        }

        const uint32_t interval_s = read_snapshot_interval_s();
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (interval_s == 0) {
            last_periodic_snapshot_ms = now_ms;
        } else if (now_ms - last_periodic_snapshot_ms >= static_cast<int64_t>(interval_s) * 1000) {
            append_snapshot();
            last_periodic_snapshot_ms = now_ms;
        }

        vTaskDelay(WORKER_POLL_TICKS);
    }
}

} // namespace

esp_err_t start_worker() {
    BaseType_t ret = xTaskCreate(blackbox_service_task, "bb_service_t", 3072, nullptr, 3, nullptr);
    return ret == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

} // namespace Internal
} // namespace BlackboxService
