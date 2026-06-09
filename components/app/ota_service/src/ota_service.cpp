#include "ota_service.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

#include "diagnostic_log.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ota_manager.h"

namespace OtaService {
namespace {

constexpr char TAG[] = "OtaService";
constexpr char CONFIG_URL[] =
    "https://cdn.jsdelivr.net/gh/qingmeijiupiao/Wireless_power_meter_lite/.github/workflows/config.toml";
constexpr size_t CONFIG_BUFFER_SIZE = 4096;
constexpr size_t DOWNLOAD_BUFFER_SIZE = 4096;
constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t TASK_STACK_SIZE = 10240;

enum class Operation : uint8_t {
    CHECK,
    UPGRADE,
};

struct Version {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
};

SemaphoreHandle_t status_mutex = nullptr;
TaskHandle_t worker_task = nullptr;
Status status = {};
char config_buffer[CONFIG_BUFFER_SIZE];

void lock_status() {
    xSemaphoreTake(status_mutex, portMAX_DELAY);
}

void unlock_status() {
    xSemaphoreGive(status_mutex);
}

void set_state(State state, const char* error = "") {
    lock_status();
    const State old_state = status.state;
    status.state = state;
    snprintf(status.last_error, sizeof(status.last_error), "%s", error == nullptr ? "" : error);
    unlock_status();
    if (old_state == state && (error == nullptr || error[0] == '\0')) {
        return;
    }
    if (state == State::FAILED) {
        DEVICE_STATE_W(TAG, "ota: state old=%s new=%s reason=%s",
                       state_to_string(old_state),
                       state_to_string(state),
                       error == nullptr || error[0] == '\0' ? "unknown" : error);
    } else {
        DEVICE_STATE_I(TAG, "ota: state old=%s new=%s result=ok",
                       state_to_string(old_state), state_to_string(state));
    }
}

void set_active_source(const char* source) {
    lock_status();
    snprintf(status.active_source, sizeof(status.active_source), "%s", source == nullptr ? "" : source);
    unlock_status();
}

void set_progress(size_t downloaded, size_t image_size) {
    lock_status();
    status.bytes_downloaded = downloaded;
    status.image_size = image_size;
    unlock_status();
}

bool parse_version(const char* text, Version* version) {
    if (text == nullptr || version == nullptr) {
        return false;
    }
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    const char* start = text[0] == 'v' ? text + 1 : text;
    char tail = '\0';
    if (sscanf(start, "%u.%u.%u%c", &major, &minor, &patch, &tail) != 3) {
        return false;
    }
    version->major = major;
    version->minor = minor;
    version->patch = patch;
    return true;
}

int compare_version(const Version& lhs, const Version& rhs) {
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (lhs.patch != rhs.patch) {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    return 0;
}

esp_http_client_handle_t open_http(const char* url, int64_t* content_length) {
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.buffer_size = DOWNLOAD_BUFFER_SIZE;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    config.max_redirection_count = 5;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        return nullptr;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return nullptr;
    }

    const int64_t length = esp_http_client_fetch_headers(client);
    const int status_code = esp_http_client_get_status_code(client);
    if (length < 0 || status_code < 200 || status_code >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return nullptr;
    }
    if (content_length != nullptr) {
        *content_length = length;
    }
    return client;
}

void close_http(esp_http_client_handle_t client) {
    if (client == nullptr) {
        return;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

esp_err_t fetch_config(char* output, size_t output_size) {
    set_active_source("jsdelivr");
    DEVICE_EVENT_I(TAG, "ota: remote_config_attempt source=jsdelivr url=%s", CONFIG_URL);

    int64_t content_length = 0;
    esp_http_client_handle_t client = open_http(CONFIG_URL, &content_length);
    if (client == nullptr) {
        ESP_LOGW(TAG, "ota: remote_config source=jsdelivr result=failed reason=http_open");
        return ESP_FAIL;
    }
    if (content_length >= static_cast<int64_t>(output_size)) {
        close_http(client);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t total = 0;
    while (total + 1 < output_size) {
        const int read = esp_http_client_read(
            client, output + total, static_cast<int>(output_size - total - 1));
        if (read < 0) {
            close_http(client);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        total += static_cast<size_t>(read);
    }
    close_http(client);
    output[total] = '\0';
    if (total == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    DEVICE_EVENT_I(TAG, "ota: remote_config source=jsdelivr result=ok bytes=%u",
                   static_cast<unsigned>(total));
    return ESP_OK;
}

bool extract_latest_version(const char* config, char* version, size_t version_size) {
    constexpr char KEY[] = "firmware_images_url";
    const char* key = strstr(config, KEY);
    if (key == nullptr) {
        return false;
    }

    const char* value_start = strchr(key, '"');
    if (value_start == nullptr) {
        return false;
    }
    value_start++;
    const char* value_end = strchr(value_start, '"');
    if (value_end == nullptr || value_start == value_end) {
        return false;
    }

    // firmware_images_url may end in either ".../vX.Y.Z" or ".../vX.Y.Z/".
    while (value_end > value_start && value_end[-1] == '/') {
        value_end--;
    }
    const char* version_start = value_end;
    while (version_start > value_start && version_start[-1] != '/') {
        version_start--;
    }
    if (version_start == value_start ||
        version_start == value_end ||
        static_cast<size_t>(value_end - version_start) >= version_size) {
        return false;
    }

    memcpy(version, version_start, static_cast<size_t>(value_end - version_start));
    version[value_end - version_start] = '\0';
    Version parsed = {};
    return parse_version(version, &parsed);
}

esp_err_t check_latest_version() {
    set_state(State::CHECKING);
    set_progress(0, 0);

    constexpr time_t MIN_VALID_TLS_TIME = 1704067200; // 2024-01-01 UTC
    if (time(nullptr) < MIN_VALID_TLS_TIME) {
        set_state(State::FAILED, "time_not_synchronized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = fetch_config(config_buffer, sizeof(config_buffer));
    if (err != ESP_OK) {
        set_state(State::FAILED, "config_download_failed");
        return err;
    }

    char latest[32] = {};
    if (!extract_latest_version(config_buffer, latest, sizeof(latest))) {
        ESP_LOGW(TAG, "remote config version parse failed");
        set_state(State::FAILED, "config_version_invalid");
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "remote config parsed latest=%s", latest);

    const esp_app_desc_t* running = esp_app_get_description();
    Version current_version = {};
    Version latest_version = {};
    if (!parse_version(running->version, &current_version) ||
        !parse_version(latest, &latest_version)) {
        set_state(State::FAILED, "version_invalid");
        return ESP_ERR_INVALID_VERSION;
    }

    lock_status();
    const State previous_state = status.state;
    snprintf(status.current_version, sizeof(status.current_version), "%s", running->version);
    snprintf(status.latest_version, sizeof(status.latest_version), "%s", latest);
    status.active_source[0] = '\0';
    status.last_error[0] = '\0';
    status.state = compare_version(latest_version, current_version) > 0
        ? State::UPDATE_AVAILABLE
        : State::UP_TO_DATE;
    const State result_state = status.state;
    unlock_status();

    DEVICE_STATE_I(TAG, "ota: state old=%s new=%s result=ok",
                   state_to_string(previous_state), state_to_string(result_state));
    DEVICE_EVENT_I(TAG, "ota: remote_check current=%s latest=%s result=%s",
                   running->version, latest, state_to_string(result_state));
    return ESP_OK;
}

void build_firmware_url(size_t source_index, const char* version, char* output, size_t output_size) {
    const char* prefix = "";
    const char* source = "github";
    if (source_index == 1) {
        prefix = "https://gh-proxy.com/";
        source = "gh-proxy";
    } else if (source_index == 2) {
        prefix = "https://ghproxy.net/";
        source = "ghproxy";
    }
    set_active_source(source);
    snprintf(output, output_size,
             "%shttps://github.com/qingmeijiupiao/Wireless_power_meter_lite/releases/download/%s/"
             "Wireless_power_meter_lite_app_%s.bin",
             prefix, version, version);
}

struct DownloadContext {
    esp_err_t error;
    size_t total;
    size_t image_size;
    bool ota_started;
};

esp_err_t firmware_http_event(esp_http_client_event_t* event) {
    auto* context = static_cast<DownloadContext*>(event->user_data);
    if (context == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) {
        return ESP_OK;
    }

    if (!context->ota_started) {
        const int64_t content_length = esp_http_client_get_content_length(event->client);
        context->image_size = content_length > 0 ? static_cast<size_t>(content_length) : 0;
        const size_t ota_size = context->image_size > 0
            ? context->image_size
            : OtaManager::IMAGE_SIZE_UNKNOWN;
        context->error = OtaManager::begin(ota_size);
        if (context->error != ESP_OK) {
            return context->error;
        }
        context->ota_started = true;
        set_progress(0, context->image_size);
    }

    context->error = OtaManager::write(event->data, static_cast<size_t>(event->data_len));
    if (context->error != ESP_OK) {
        return context->error;
    }
    context->total += static_cast<size_t>(event->data_len);
    set_progress(context->total, context->image_size);
    return ESP_OK;
}

esp_err_t download_from_source(size_t source_index, const char* version) {
    char url[320] = {};
    build_firmware_url(source_index, version, url, sizeof(url));
    const Status before = get_status();
    DEVICE_EVENT_I(TAG, "ota: firmware_attempt source=%s version=%s url=%s",
                   before.active_source, version, url);

    DownloadContext context = {
        .error = ESP_OK,
        .total = 0,
        .image_size = 0,
        .ota_started = false,
    };
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    config.buffer_size = DOWNLOAD_BUFFER_SIZE;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = firmware_http_event;
    config.user_data = &context;
    config.keep_alive_enable = true;
    config.max_redirection_count = 5;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGW(TAG, "ota: firmware source=%s result=failed reason=http_init",
                 before.active_source);
        return ESP_ERR_NO_MEM;
    }

    set_state(State::DOWNLOADING);
    set_active_source(before.active_source);
    set_progress(0, 0);
    esp_err_t err = esp_http_client_perform(client);
    const int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK) {
        err = context.error;
    }
    if (err == ESP_OK && (status_code < 200 || status_code >= 300 || !context.ota_started)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK && context.image_size > 0 && context.total != context.image_size) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err != ESP_OK) {
        if (context.ota_started) {
            OtaManager::abort();
        }
        ESP_LOGW(TAG, "ota: firmware source=%s result=failed bytes=%u/%u err=%s",
                 before.active_source,
                 static_cast<unsigned>(context.total),
                 static_cast<unsigned>(context.image_size),
                 esp_err_to_name(err));
        return err;
    }

    set_state(State::VERIFYING);
    set_active_source(before.active_source);
    err = OtaManager::finish();
    if (err == ESP_OK) {
        err = OtaManager::activate();
    }
    if (err != ESP_OK) {
        if (OtaManager::get_status().state == OtaManager::State::VERIFIED) {
            OtaManager::abort();
        }
        ESP_LOGE(TAG, "ota: firmware_verify source=%s result=failed err=%s",
                 before.active_source, esp_err_to_name(err));
        return err;
    }

    DEVICE_STATE_I(TAG, "ota: firmware source=%s result=activated version=%s bytes=%u",
                   before.active_source, version, static_cast<unsigned>(context.total));
    return ESP_OK;
}

esp_err_t perform_upgrade() {
    if (OtaManager::get_status().state != OtaManager::State::IDLE) {
        set_state(State::FAILED, "ota_busy");
        return ESP_ERR_INVALID_STATE;
    }

    Status snapshot = get_status();
    if (snapshot.state != State::UPDATE_AVAILABLE) {
        esp_err_t err = check_latest_version();
        if (err != ESP_OK) {
            return err;
        }
        snapshot = get_status();
    }
    if (snapshot.state != State::UPDATE_AVAILABLE) {
        return ESP_ERR_INVALID_VERSION;
    }

    esp_err_t last_error = ESP_FAIL;
    for (size_t source = 0; source < 3; ++source) {
        last_error = download_from_source(source, snapshot.latest_version);
        if (last_error == ESP_OK) {
            set_state(State::RESTARTING);
            DEVICE_EVENT_I(TAG, "ota: restart delay_ms=2000 reason=upgrade_complete");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
    }

    set_state(State::FAILED, esp_err_to_name(last_error));
    set_active_source("");
    ESP_LOGE(TAG, "all firmware sources failed: %s", esp_err_to_name(last_error));
    return last_error;
}

void worker(void* argument) {
    const Operation operation = *static_cast<Operation*>(argument);
    delete static_cast<Operation*>(argument);

    if (operation == Operation::CHECK) {
        check_latest_version();
    } else {
        perform_upgrade();
    }

    lock_status();
    worker_task = nullptr;
    unlock_status();
    vTaskDelete(nullptr);
}

esp_err_t start_operation(Operation operation) {
    ESP_RETURN_ON_ERROR(init(), TAG, "init failed");

    lock_status();
    if (worker_task != nullptr) {
        unlock_status();
        return ESP_ERR_INVALID_STATE;
    }
    Operation* argument = new (std::nothrow) Operation(operation);
    if (argument == nullptr) {
        unlock_status();
        return ESP_ERR_NO_MEM;
    }
    const BaseType_t result = xTaskCreate(
        worker, "ota_remote", TASK_STACK_SIZE, argument, 3, &worker_task);
    if (result != pdPASS) {
        worker_task = nullptr;
        delete argument;
        unlock_status();
        return ESP_ERR_NO_MEM;
    }
    unlock_status();
    return ESP_OK;
}

} // namespace

esp_err_t init() {
    if (status_mutex != nullptr) {
        return ESP_OK;
    }
    status_mutex = xSemaphoreCreateMutex();
    if (status_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    const esp_app_desc_t* running = esp_app_get_description();
    status.state = State::IDLE;
    snprintf(status.current_version, sizeof(status.current_version), "%s", running->version);
    return ESP_OK;
}

esp_err_t request_check() {
    return start_operation(Operation::CHECK);
}

esp_err_t request_upgrade() {
    return start_operation(Operation::UPGRADE);
}

Status get_status() {
    Status snapshot = {};
    if (status_mutex == nullptr) {
        const esp_app_desc_t* running = esp_app_get_description();
        snapshot.state = State::IDLE;
        snprintf(snapshot.current_version, sizeof(snapshot.current_version), "%s", running->version);
        return snapshot;
    }
    lock_status();
    snapshot = status;
    unlock_status();
    return snapshot;
}

const char* state_to_string(State state) {
    switch (state) {
        case State::IDLE: return "idle";
        case State::CHECKING: return "checking";
        case State::UPDATE_AVAILABLE: return "update_available";
        case State::UP_TO_DATE: return "up_to_date";
        case State::DOWNLOADING: return "downloading";
        case State::VERIFYING: return "verifying";
        case State::RESTARTING: return "restarting";
        case State::FAILED: return "failed";
        default: return "unknown";
    }
}

} // namespace OtaService
