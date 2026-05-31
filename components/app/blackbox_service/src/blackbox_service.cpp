/*
 * @Description: 应用层黑匣子服务入口与关键事件接口
 */
#include "blackbox_service.h"

#include <cstdarg>
#include <cstdio>

#include "blackbox_service_internal.h"

namespace BlackboxService {
namespace {

bool initialized = false;

} // namespace

esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    Internal::load_config();
    esp_err_t ret = Internal::start_worker();
    if (ret != ESP_OK) {
        return ret;
    }

    Internal::install_log_capture();
    initialized = true;
    return ESP_OK;
}

esp_err_t append_event(const char* fmt, ...) {
    if (fmt == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char text[Blackbox::TEXT_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    const esp_err_t text_ret = Blackbox::append_text("%s", text);
    const esp_err_t snapshot_ret = append_snapshot();
    return snapshot_ret != ESP_OK ? snapshot_ret : text_ret;
}

uint32_t get_snapshot_interval_s() {
    return Internal::read_snapshot_interval_s();
}

void set_snapshot_interval_s(uint32_t seconds) {
    Internal::write_snapshot_interval_s(seconds);
    append_event("blackbox: snapshot_interval_s=%lu", static_cast<unsigned long>(seconds));
}

} // namespace BlackboxService
