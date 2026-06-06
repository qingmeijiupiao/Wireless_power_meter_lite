#include "web_backend_internal.h"

#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ota_manager.h"
#include "ota_service.h"
#include "blackbox_service.h"

namespace WebBackend {
namespace {

constexpr const char* TAG = "WebBackendOta";
constexpr size_t OTA_UPLOAD_BUFFER_SIZE = 4096;
char ota_upload_buffer[OTA_UPLOAD_BUFFER_SIZE];

const char* ota_state_to_str(OtaManager::State state) {
    switch (state) {
        case OtaManager::State::IDLE: return "idle";
        case OtaManager::State::WRITING: return "writing";
        case OtaManager::State::VERIFIED: return "verified";
        case OtaManager::State::READY_TO_REBOOT: return "ready_to_reboot";
        default: return "unknown";
    }
}

const char* ota_error_to_str(esp_err_t err) {
    switch (err) {
        case ESP_OK: return "ok";
        case ESP_ERR_INVALID_ARG: return "invalid_argument";
        case ESP_ERR_INVALID_STATE: return "invalid_state";
        case ESP_ERR_INVALID_SIZE: return "invalid_size";
        case ESP_ERR_NO_MEM: return "no_memory";
        case ESP_ERR_NOT_FOUND: return "partition_not_found";
        case ESP_ERR_TIMEOUT: return "receive_timeout";
        case ESP_ERR_OTA_PARTITION_CONFLICT: return "partition_conflict";
        case ESP_ERR_OTA_VALIDATE_FAILED: return "image_validation_failed";
        case ESP_ERR_OTA_ROLLBACK_INVALID_STATE: return "running_firmware_not_confirmed";
        default: return esp_err_to_name(err);
    }
}

const esp_partition_t* status_target_partition() {
    const esp_partition_t* target = OtaManager::get_target_partition();
    return target == nullptr ? OtaManager::get_next_update_partition() : target;
}

void append_ota_status_event(const char* phase, esp_err_t err = ESP_OK) {
    const OtaManager::Status status = OtaManager::get_status();
    BlackboxService::append_text_event(
        "ota: %s state=%s bytes=%u/%u slots(run=%u boot=%u target=%u) err=%s(0x%x)",
        phase,
        ota_state_to_str(status.state),
        static_cast<unsigned>(status.bytes_written),
        static_cast<unsigned>(status.image_size),
        static_cast<unsigned>(ota_partition_slot(OtaManager::get_running_partition())),
        static_cast<unsigned>(ota_partition_slot(OtaManager::get_boot_partition())),
        static_cast<unsigned>(ota_partition_slot(status_target_partition())),
        ota_error_to_str(err),
        static_cast<unsigned>(err));
}

void append_partition_json(char* out, size_t out_size, const esp_partition_t* partition) {
    snprintf(out, out_size,
        "{\"slot\":%u,\"label\":\"%s\",\"size\":%u}",
        static_cast<unsigned>(ota_partition_slot(partition)),
        partition == nullptr ? "" : partition->label,
        partition == nullptr ? 0U : static_cast<unsigned>(partition->size));
}

esp_err_t send_ota_status(WebServer::Request* request, bool ok, const char* reason) {
    const OtaManager::Status status = OtaManager::get_status();
    const OtaService::Status remote = OtaService::get_status();
    const esp_partition_t* running = OtaManager::get_running_partition();
    const esp_partition_t* boot = OtaManager::get_boot_partition();
    const esp_partition_t* target = status_target_partition();
    char running_json[96] = {};
    char boot_json[96] = {};
    char target_json[96] = {};
    char target_version[32] = {};
    append_partition_json(running_json, sizeof(running_json), running);
    append_partition_json(boot_json, sizeof(boot_json), boot);
    append_partition_json(target_json, sizeof(target_json), target);

    esp_app_desc_t target_desc = {};
    if (OtaManager::get_target_app_description(&target_desc) == ESP_OK) {
        strncpy(target_version, target_desc.version, sizeof(target_version) - 1);
    }

    snprintf(detail_response_buffer, sizeof(detail_response_buffer),
        "{"
        "\"ok\":%s,"
        "\"reason\":\"%s\","
        "\"state\":\"%s\","
        "\"image_size\":%u,"
        "\"bytes_written\":%u,"
        "\"max_image_size\":%u,"
        "\"target_version\":\"%s\","
        "\"running\":%s,"
        "\"boot\":%s,"
        "\"target\":%s,"
        "\"remote\":{"
        "\"state\":\"%s\","
        "\"current_version\":\"%s\","
        "\"latest_version\":\"%s\","
        "\"source\":\"%s\","
        "\"last_error\":\"%s\","
        "\"bytes_downloaded\":%u,"
        "\"image_size\":%u"
        "}"
        "}\n",
        ok ? "true" : "false",
        reason,
        ota_state_to_str(status.state),
        static_cast<unsigned>(status.image_size),
        static_cast<unsigned>(status.bytes_written),
        target == nullptr ? 0U : static_cast<unsigned>(target->size),
        target_version,
        running_json,
        boot_json,
        target_json,
        OtaService::state_to_string(remote.state),
        remote.current_version,
        remote.latest_version,
        remote.active_source,
        remote.last_error,
        static_cast<unsigned>(remote.bytes_downloaded),
        static_cast<unsigned>(remote.image_size));
    return WebServer::send_json(request, detail_response_buffer);
}

void ota_reboot_timer_callback(void*) {
    esp_restart();
}

esp_err_t schedule_ota_reboot() {
    static esp_timer_handle_t reboot_timer = nullptr;
    if (reboot_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = ota_reboot_timer_callback;
        args.name = "ota_reboot";
        esp_err_t err = esp_timer_create(&args, &reboot_timer);
        if (err != ESP_OK) {
            return err;
        }
    }
    esp_timer_stop(reboot_timer);
    return esp_timer_start_once(reboot_timer, 1000000);
}

} // namespace

uint8_t ota_partition_slot(const esp_partition_t* partition) {
    if (partition == nullptr || partition->type != ESP_PARTITION_TYPE_APP) {
        return 0;
    }
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        return 1;
    }
    if (partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        return 2;
    }
    return 0;
}

/** @brief GET /api/ota/status，返回 OTA 会话、容量和分区信息。 */
esp_err_t ota_status_handler(WebServer::Request* request) {
    return send_ota_status(request, true, "ok");
}

/** @brief POST /api/ota/upload，以 application/octet-stream 流式写入 APP 固件。 */
esp_err_t ota_upload_handler(WebServer::Request* request) {
    const size_t image_size = request->raw->content_len;
    if (image_size == 0) {
        BlackboxService::append_text_event("ota: upload_rejected reason=empty_image");
        return WebServer::send(request, 400, "application/json",
            "{\"ok\":false,\"reason\":\"empty_image\"}\n",
            strlen("{\"ok\":false,\"reason\":\"empty_image\"}\n"));
    }

    esp_err_t err = OtaManager::begin(image_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA begin failed: %s", ota_error_to_str(err));
        const OtaManager::Status status = OtaManager::get_status();
        BlackboxService::append_text_event(
            "ota: begin_failed request=%u state=%s slots(run=%u boot=%u target=%u) err=%s(0x%x)",
            static_cast<unsigned>(image_size),
            ota_state_to_str(status.state),
            static_cast<unsigned>(ota_partition_slot(OtaManager::get_running_partition())),
            static_cast<unsigned>(ota_partition_slot(OtaManager::get_boot_partition())),
            static_cast<unsigned>(ota_partition_slot(status_target_partition())),
            ota_error_to_str(err),
            static_cast<unsigned>(err));
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, err == ESP_ERR_INVALID_SIZE ? 413 : 409,
            "application/json", response_buffer, strlen(response_buffer));
    }
    append_ota_status_event("upload_started");

    uint8_t next_progress_percent = 25;
    err = WebServer::stream_body(request, ota_upload_buffer, sizeof(ota_upload_buffer),
        [image_size, &next_progress_percent](const char* data, size_t size) -> esp_err_t {
            const esp_err_t write_err = OtaManager::write(data, size);
            if (write_err != ESP_OK) {
                return write_err;
            }

            const OtaManager::Status status = OtaManager::get_status();
            while (next_progress_percent <= 75 &&
                   status.bytes_written * 100 >= image_size * next_progress_percent) {
                BlackboxService::append_text_event(
                    "ota: upload_progress percent=%u bytes=%u/%u target=%u",
                    static_cast<unsigned>(next_progress_percent),
                    static_cast<unsigned>(status.bytes_written),
                    static_cast<unsigned>(image_size),
                    static_cast<unsigned>(ota_partition_slot(status.target_partition)));
                next_progress_percent += 25;
            }
            return ESP_OK;
        });
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA upload interrupted: %s", ota_error_to_str(err));
        const OtaManager::Status status = OtaManager::get_status();
        const esp_err_t abort_err = OtaManager::abort();
        BlackboxService::append_text_event(
            "ota: upload_interrupted state=%s bytes=%u/%u target=%u err=%s(0x%x) abort=%s(0x%x)",
            ota_state_to_str(status.state),
            static_cast<unsigned>(status.bytes_written),
            static_cast<unsigned>(status.image_size),
            static_cast<unsigned>(ota_partition_slot(status.target_partition)),
            ota_error_to_str(err),
            static_cast<unsigned>(err),
            ota_error_to_str(abort_err),
            static_cast<unsigned>(abort_err));
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, 500, "application/json", response_buffer, strlen(response_buffer));
    }

    append_ota_status_event("upload_received");
    const OtaManager::Status received_status = OtaManager::get_status();
    err = OtaManager::finish();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA validation failed: %s", ota_error_to_str(err));
        BlackboxService::append_text_event("ota: validation_failed bytes=%u/%u target=%u err=%s(0x%x)",
                                           static_cast<unsigned>(received_status.bytes_written),
                                           static_cast<unsigned>(received_status.image_size),
                                           static_cast<unsigned>(ota_partition_slot(received_status.target_partition)),
                                           ota_error_to_str(err),
                                           static_cast<unsigned>(err));
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, 400, "application/json", response_buffer, strlen(response_buffer));
    }

    esp_app_desc_t target_desc = {};
    err = OtaManager::get_target_app_description(&target_desc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA APP description unavailable: %s", ota_error_to_str(err));
        const OtaManager::Status status = OtaManager::get_status();
        const esp_err_t abort_err = OtaManager::abort();
        BlackboxService::append_text_event(
            "ota: app_desc_failed state=%s bytes=%u/%u target=%u err=%s(0x%x) abort=%s(0x%x)",
            ota_state_to_str(status.state),
            static_cast<unsigned>(status.bytes_written),
            static_cast<unsigned>(status.image_size),
            static_cast<unsigned>(ota_partition_slot(status.target_partition)),
            ota_error_to_str(err),
            static_cast<unsigned>(err),
            ota_error_to_str(abort_err),
            static_cast<unsigned>(abort_err));
        return WebServer::send(request, 422, "application/json",
            "{\"ok\":false,\"reason\":\"app_description_unavailable\"}\n",
            strlen("{\"ok\":false,\"reason\":\"app_description_unavailable\"}\n"));
    }

    ESP_LOGI(TAG, "OTA image verified, waiting for activation");
    BlackboxService::append_text_event("ota: verified version=%s bytes=%u target=%u",
                                       target_desc.version,
                                       static_cast<unsigned>(image_size),
                                       static_cast<unsigned>(ota_partition_slot(OtaManager::get_target_partition())));
    return send_ota_status(request, true, "verified");
}

/** @brief POST /api/ota/activate，确认切换分区并安排重启。 */
esp_err_t ota_activate_handler(WebServer::Request* request) {
    esp_err_t err = OtaManager::activate();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA activate failed: %s", ota_error_to_str(err));
        append_ota_status_event("activate_failed", err);
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, 409, "application/json", response_buffer, strlen(response_buffer));
    }

    err = schedule_ota_reboot();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA reboot scheduling failed: %s", ota_error_to_str(err));
        append_ota_status_event("reboot_schedule_failed", err);
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, 500, "application/json", response_buffer, strlen(response_buffer));
    }

    ESP_LOGW(TAG, "OTA activated, restarting in 1 second");
    append_ota_status_event("activated_reboot_in_1000ms");
    return send_ota_status(request, true, "restarting");
}

/** @brief POST /api/ota/abort，中止上传或放弃尚未激活的固件。 */
esp_err_t ota_abort_handler(WebServer::Request* request) {
    const OtaManager::Status status = OtaManager::get_status();
    esp_err_t err = OtaManager::abort();
    if (err != ESP_OK) {
        BlackboxService::append_text_event("ota: abort_failed state=%s bytes=%u/%u err=%s(0x%x)",
                                           ota_state_to_str(status.state),
                                           static_cast<unsigned>(status.bytes_written),
                                           static_cast<unsigned>(status.image_size),
                                           ota_error_to_str(err),
                                           static_cast<unsigned>(err));
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, 409, "application/json", response_buffer, strlen(response_buffer));
    }
    BlackboxService::append_text_event("ota: aborted previous_state=%s bytes=%u/%u target=%u",
                                       ota_state_to_str(status.state),
                                       static_cast<unsigned>(status.bytes_written),
                                       static_cast<unsigned>(status.image_size),
                                       static_cast<unsigned>(ota_partition_slot(status.target_partition)));
    return send_ota_status(request, true, "aborted");
}

/** @brief GET /api/ota/remote/check，异步检查远端最新版本。 */
esp_err_t ota_remote_check_handler(WebServer::Request* request) {
    const esp_err_t err = OtaService::request_check();
    if (err != ESP_OK) {
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, err == ESP_ERR_INVALID_STATE ? 409 : 500,
                               "application/json", response_buffer, strlen(response_buffer));
    }
    return send_ota_status(request, true, "checking");
}

/** @brief POST /api/ota/remote/download，异步在线升级并在成功后自动重启。 */
esp_err_t ota_remote_download_handler(WebServer::Request* request) {
    const esp_err_t err = OtaService::request_upgrade();
    if (err != ESP_OK) {
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":false,\"reason\":\"%s\"}\n", ota_error_to_str(err));
        return WebServer::send(request, err == ESP_ERR_INVALID_STATE ? 409 : 500,
                               "application/json", response_buffer, strlen(response_buffer));
    }
    BlackboxService::append_text_event("ota: remote_upgrade_requested source=web");
    return send_ota_status(request, true, "upgrade_started");
}

} // namespace WebBackend
