#include "web_backend.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cinttypes>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "global_state.h"
#include "power_output.h"
#include "protect.h"
#include "st7735.h"
#include "can_callback.h"
#include "current_calibration.h"
#include "hardware.h"
#include "web_file.h"
#include "web_server.h"
#include "wifi_service.h"

namespace WebBackend {

static const char* TAG = "WebBackend";
static bool initialized = false;
static bool running = false;
static char response_buffer[1536];
static char detail_response_buffer[4096];
static char scan_response_buffer[3072];

static bool append_checked(char* out, size_t out_size, size_t* pos, const char* fmt, ...);

static constexpr size_t LOG_RING_SIZE = 8 * 1024;
static constexpr size_t LOG_LINE_MAX_LEN = 256;
static constexpr size_t LOG_RESPONSE_RAW_MAX = 2400;
static char log_ring[LOG_RING_SIZE];
static char log_snapshot_buffer[LOG_RESPONSE_RAW_MAX + 1];
static portMUX_TYPE log_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t log_ring_write_pos = 0;
static size_t log_ring_used = 0;
static uint64_t log_ring_seq = 0;
static vprintf_like_t original_log_vprintf = nullptr;
static bool log_capture_installed = false;

static const char* mode_to_str(WifiService::Mode mode) {
    switch (mode) {
        case WifiService::Mode::OFF: return "off";
        case WifiService::Mode::STA: return "sta";
        case WifiService::Mode::AP_PROVISION: return "ap_provision";
        default: return "unknown";
    }
}

static const char* output_result_to_str(PowerOutput::OutputResult result) {
    switch (result) {
        case PowerOutput::OutputResult::OK: return "ok";
        case PowerOutput::OutputResult::FAIL_NOT_INIT: return "not_initialized";
        case PowerOutput::OutputResult::FAIL_PROTECT_ACTIVE: return "protect_active";
        case PowerOutput::OutputResult::FAIL_COOLDOWN_ACTIVE: return "cooldown_active";
        default: return "unknown";
    }
}

static void ip_to_str(IP_t ip, char* out, size_t out_size) {
    snprintf(out, out_size, "%u.%u.%u.%u", ip.octet1, ip.octet2, ip.octet3, ip.octet4);
}

static void mac_to_str(MAC_t mac, char* out, size_t out_size) {
    snprintf(out, out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
        mac.octet1, mac.octet2, mac.octet3, mac.octet4, mac.octet5, mac.octet6);
}

static void log_ring_write(const char* text, size_t len) {
    if (text == nullptr || len == 0) {
        return;
    }

    portENTER_CRITICAL(&log_ring_lock);
    for (size_t i = 0; i < len; ++i) {
        log_ring[log_ring_write_pos] = text[i];
        log_ring_write_pos = (log_ring_write_pos + 1) % LOG_RING_SIZE;
        if (log_ring_used < LOG_RING_SIZE) {
            log_ring_used++;
        }
        log_ring_seq++;
    }
    portEXIT_CRITICAL(&log_ring_lock);
}

static int log_capture_vprintf(const char* fmt, va_list args) {
    int ret = 0;
    if (original_log_vprintf != nullptr) {
        va_list out_args;
        va_copy(out_args, args);
        ret = original_log_vprintf(fmt, out_args);
        va_end(out_args);
    }

    va_list copy_args;
    va_copy(copy_args, args);
    char line[LOG_LINE_MAX_LEN];
    int len = vsnprintf(line, sizeof(line), fmt, copy_args);
    va_end(copy_args);

    if (len > 0) {
        size_t write_len = static_cast<size_t>(len);
        if (write_len >= sizeof(line)) {
            write_len = sizeof(line) - 1;
        }
        log_ring_write(line, write_len);
    }
    return ret;
}

static void install_log_capture() {
    if (log_capture_installed) {
        return;
    }
    original_log_vprintf = esp_log_set_vprintf(log_capture_vprintf);
    log_capture_installed = true;
}

static void clear_log_ring() {
    portENTER_CRITICAL(&log_ring_lock);
    log_ring_write_pos = 0;
    log_ring_used = 0;
    log_ring_seq = 0;
    portEXIT_CRITICAL(&log_ring_lock);
}

static size_t read_log_ring(uint64_t since, char* out, size_t out_size, uint64_t* from_seq, uint64_t* next_seq, uint64_t* latest_seq, bool* dropped) {
    if (out == nullptr || out_size == 0 || from_seq == nullptr || next_seq == nullptr || latest_seq == nullptr || dropped == nullptr) {
        return 0;
    }

    portENTER_CRITICAL(&log_ring_lock);
    uint64_t oldest_seq = log_ring_seq > log_ring_used ? log_ring_seq - log_ring_used : 0;
    uint64_t start_seq = since;
    bool was_dropped = false;
    if (start_seq < oldest_seq) {
        start_seq = oldest_seq;
        was_dropped = true;
    }
    if (start_seq > log_ring_seq) {
        start_seq = log_ring_seq;
    }

    size_t available = static_cast<size_t>(log_ring_seq - start_seq);
    if (available > out_size - 1) {
        available = out_size - 1;
    }

    size_t oldest_pos = (log_ring_write_pos + LOG_RING_SIZE - log_ring_used) % LOG_RING_SIZE;
    size_t offset = static_cast<size_t>(start_seq - oldest_seq);
    size_t pos = (oldest_pos + offset) % LOG_RING_SIZE;
    for (size_t i = 0; i < available; ++i) {
        out[i] = log_ring[(pos + i) % LOG_RING_SIZE];
    }
    out[available] = '\0';
    *from_seq = start_seq;
    *next_seq = start_seq + available;
    *latest_seq = log_ring_seq;
    *dropped = was_dropped;
    portEXIT_CRITICAL(&log_ring_lock);
    return available;
}

static bool json_get_string(const char* json, const char* key, char* out, size_t out_size) {
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0) {
        return false;
    }
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == nullptr) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;

    size_t used = 0;
    while (*p != '\0' && *p != '"' && used < out_size - 1) {
        if (*p == '\\' && p[1] != '\0') {
            p++;
        }
        out[used++] = *p++;
    }
    out[used] = '\0';
    return true;
}

static bool json_get_bool(const char* json, const char* key, bool* out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == nullptr) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (strncmp(p, "true", 4) == 0 || *p == '1') {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0 || *p == '0') {
        *out = false;
        return true;
    }
    return false;
}

static bool json_get_uint32(const char* json, const char* key, uint32_t* out) {
    if (json == nullptr || key == nullptr || out == nullptr) {
        return false;
    }
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (p == nullptr) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == nullptr) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    char* end = nullptr;
    unsigned long value = strtoul(p, &end, 0);
    if (end == p) {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

static esp_err_t cors_middleware(WebServer::Request* request) {
    httpd_resp_set_hdr(request->raw, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request->raw, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(request->raw, "Access-Control-Allow-Headers", "Content-Type");

    if (request->method == WebServer::Method::OPTIONS) {
        return WebServer::send(request, 204, "text/plain", "", 0);
    }
    return ESP_OK;
}

static esp_err_t log_middleware(WebServer::Request* request) {
    if (strcmp(request->uri, "/api/logs") == 0 || strcmp(request->uri, "/api/logs/clear") == 0) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "request uri=%s", request->uri);
    return ESP_OK;
}

static esp_err_t index_handler(WebServer::Request* request) {
    if (WifiService::is_provisioning()) {
        return WebServer::send_html(request, provision_html_file.data, provision_html_file.size);
    }
    return WebServer::send_html(request, index_html_file.data, index_html_file.size);
}

static esp_err_t main_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, index_html_file.data, index_html_file.size);
}

static esp_err_t charts_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, charts_html_file.data, charts_html_file.size);
}

static esp_err_t control_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, control_html_file.data, control_html_file.size);
}

static esp_err_t status_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, status_html_file.data, status_html_file.size);
}

static esp_err_t logs_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, logs_html_file.data, logs_html_file.size);
}

static esp_err_t blackbox_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, blackbox_html_file.data, blackbox_html_file.size);
}

static esp_err_t app_css_handler(WebServer::Request* request) {
    return WebServer::send(request, 200, "text/css", app_css_file.data, app_css_file.size);
}

static esp_err_t provision_handler(WebServer::Request* request) {
    return WebServer::send_html(request, provision_html_file.data, provision_html_file.size);
}

static esp_err_t state_handler(WebServer::Request* request) {
    auto& state = get_global_state();
    IP_t ip = WifiService::get_ip();
    char ip_text[16] = {};
    ip_to_str(ip, ip_text, sizeof(ip_text));

    float voltage_v = state.voltage_mV / 1000.0f;
    float current_a = state.current_uA / 1000000.0f;
    float abs_current_a = std::abs(state.current_uA) / 1000000.0f;
    float board_temp_c = state.board_temperature / 100.0f;
    float chip_temp_c = state.chip_temperature / 100.0f;
    auto& protect = state.protect_states.states_bit;

    snprintf(response_buffer, sizeof(response_buffer),
        "{"
        "\"voltage_v\":%.3f,"
        "\"current_a\":%.3f,"
        "\"power_w\":%.3f,"
        "\"board_temp_c\":%.2f,"
        "\"chip_temp_c\":%.2f,"
        "\"output_on\":%s,"
        "\"protect_bypassed\":%s,"
        "\"uptime_ms\":%lld,"
        "\"protect\":{\"otp\":%u,\"ovp\":%u,\"uvp\":%u,\"ocp\":%u},"
        "\"wifi\":{\"mode\":\"%s\",\"state\":%d,\"ip\":\"%s\",\"ap_ssid\":\"%s\",\"boot_enabled\":%s,\"last_error\":\"%s\"}"
        "}\n",
        voltage_v,
        current_a,
        voltage_v * abs_current_a,
        board_temp_c,
        chip_temp_c,
        state.global_state_bits.state_bit.out_put_state ? "true" : "false",
        protect_is_bypassed() ? "true" : "false",
        esp_timer_get_time() / 1000,
        (unsigned)protect.temperature_protect_state,
        (unsigned)protect.high_voltage_protect_state,
        (unsigned)protect.low_voltage_protect_state,
        (unsigned)protect.current_protect_state,
        mode_to_str(WifiService::get_mode()),
        (int)WifiService::get_wifi_state(),
        ip_text,
        WifiService::get_ap_ssid(),
        WifiService::is_web_enabled_on_boot() ? "true" : "false",
        WifiService::get_last_error());

    return WebServer::send_json(request, response_buffer);
}

static esp_err_t output_handler(WebServer::Request* request) {
    esp_err_t ret = WebServer::load_body(request);
    if (ret != ESP_OK) {
        return ret;
    }

    bool target = false;
    bool has_state = json_get_bool(request->body, "state", &target);
    PowerOutput::OutputResult result = PowerOutput::OutputResult::OK;
    if (strstr(request->body, "\"toggle\"") != nullptr) {
        result = PowerOutput::toggle();
    } else if (has_state) {
        result = target ? PowerOutput::on() : PowerOutput::off();
    } else {
        return WebServer::send(request, 400, "application/json", "{\"ok\":false,\"reason\":\"missing_state\"}\n", strlen("{\"ok\":false,\"reason\":\"missing_state\"}\n"));
    }

    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\",\"output_on\":%s}\n",
        result == PowerOutput::OutputResult::OK ? "true" : "false",
        output_result_to_str(result),
        PowerOutput::get_state() ? "true" : "false");
    return WebServer::send_json(request, response_buffer);
}

static void reboot_timer_callback(void* arg) {
    esp_restart();
}

static esp_err_t reboot_handler(WebServer::Request* request) {
    static esp_timer_handle_t reboot_timer = nullptr;
    if (reboot_timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = reboot_timer_callback;
        args.name = "web_reboot";
        esp_err_t ret = esp_timer_create(&args, &reboot_timer);
        if (ret != ESP_OK) {
            snprintf(response_buffer, sizeof(response_buffer), "{\"ok\":false,\"reason\":\"%s\"}\n", esp_err_to_name(ret));
            return WebServer::send_json(request, response_buffer);
        }
    }
    esp_timer_stop(reboot_timer);
    esp_timer_start_once(reboot_timer, 300000);
    return WebServer::send_json(request, "{\"ok\":true,\"reason\":\"rebooting\"}\n");
}

static esp_err_t system_handler(WebServer::Request* request) {
    const esp_app_desc_t* app_desc = esp_app_get_description();
    char sta_mac[18] = {};
    char ap_mac[18] = {};
    mac_to_str(WiFiManager::instance().get_mac(WIFI_IF_STA), sta_mac, sizeof(sta_mac));
    mac_to_str(WiFiManager::instance().get_mac(WIFI_IF_AP), ap_mac, sizeof(ap_mac));
    snprintf(detail_response_buffer, sizeof(detail_response_buffer),
        "{"
        "\"hardware_version\":%u,"
        "\"firmware\":{\"major\":%u,\"minor\":%u,\"patch\":%u,\"project\":\"%s\",\"idf\":\"%s\",\"build_date\":\"%s\",\"build_time\":\"%s\"},"
        "\"mac\":{\"sta\":\"%s\",\"ap\":\"%s\"},"
        "\"uptime_ms\":%lld"
        "}\n",
        static_cast<unsigned>(get_hardware_version()),
        static_cast<unsigned>(VERSION_MAJOR),
        static_cast<unsigned>(VERSION_MINOR),
        static_cast<unsigned>(VERSION_PATCH),
        app_desc->project_name,
        app_desc->idf_ver,
        app_desc->date,
        app_desc->time,
        sta_mac,
        ap_mac,
        esp_timer_get_time() / 1000);
    return WebServer::send_json(request, detail_response_buffer);
}

static esp_err_t backlight_handler(WebServer::Request* request) {
    if (request->method == WebServer::Method::POST) {
        esp_err_t ret = WebServer::load_body(request);
        if (ret != ESP_OK) {
            return ret;
        }
        uint32_t brightness = 0;
        if (!json_get_uint32(request->body, "brightness", &brightness) || brightness > 255) {
            return WebServer::send(request, 400, "application/json", "{\"ok\":false,\"reason\":\"invalid_brightness\"}\n", strlen("{\"ok\":false,\"reason\":\"invalid_brightness\"}\n"));
        }
        ret = ST7735::set_backlight(static_cast<uint8_t>(brightness));
        snprintf(response_buffer, sizeof(response_buffer),
            "{\"ok\":%s,\"reason\":\"%s\",\"brightness\":%u}\n",
            ret == ESP_OK ? "true" : "false",
            ret == ESP_OK ? "ok" : esp_err_to_name(ret),
            static_cast<unsigned>(ST7735::get_backlight()));
        return WebServer::send_json(request, response_buffer);
    }

    snprintf(response_buffer, sizeof(response_buffer),
        "{\"brightness\":%u}\n", static_cast<unsigned>(ST7735::get_backlight()));
    return WebServer::send_json(request, response_buffer);
}

static const char* protect_state_to_str(ProtectState_t state) {
    switch (state) {
        case PROTECT_STATE_NORMAL: return "normal";
        case PROTECT_STATE_WARNING: return "warning";
        case PROTECT_STATE_PROTECT: return "protect";
        default: return "unknown";
    }
}

static esp_err_t protect_handler(WebServer::Request* request) {
    if (request->method == WebServer::Method::POST) {
        esp_err_t ret = WebServer::load_body(request);
        if (ret != ESP_OK) {
            return ret;
        }
        bool enabled = true;
        if (!json_get_bool(request->body, "enabled", &enabled)) {
            return WebServer::send(request, 400, "application/json", "{\"ok\":false,\"reason\":\"missing_enabled\"}\n", strlen("{\"ok\":false,\"reason\":\"missing_enabled\"}\n"));
        }
        protect_set_bypassed(!enabled);
        if (enabled && protect_should_block_output()) {
            PowerOutput::off();
        }
    }

    size_t pos = 0;
    bool ok = append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos,
        "{\"enabled\":%s,\"bypassed\":%s,\"active_fault\":%s,\"should_block_output\":%s,\"channels\":[",
        protect_is_bypassed() ? "false" : "true",
        protect_is_bypassed() ? "true" : "false",
        protect_has_active_fault() ? "true" : "false",
        protect_should_block_output() ? "true" : "false");

    for (uint8_t i = 0; ok && i < protect_get_channel_count(); ++i) {
        protect_channel_info_t info = {};
        if (!protect_get_channel_info(i, &info)) {
            continue;
        }
        ok = append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos,
            "%s{\"name\":\"%s\",\"unit\":\"%s\",\"value\":%.3f,\"state\":%u,\"state_text\":\"%s\","
            "\"warning\":%.3f,\"warning_recovery\":%.3f,\"protect\":%.3f,\"protect_recovery\":%.3f,\"trigger\":\"%s\"}",
            i == 0 ? "" : ",",
            info.name,
            info.unit,
            info.now_value,
            static_cast<unsigned>(info.state),
            protect_state_to_str(info.state),
            info.threshold.warning_threshold,
            info.threshold.warning_recovery_threshold,
            info.threshold.protect_threshold,
            info.threshold.protect_recovery_threshold,
            info.threshold.is_asc ? ">=" : "<=");
    }

    if (!ok || !append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos, "]}\n")) {
        snprintf(detail_response_buffer, sizeof(detail_response_buffer), "{\"error\":\"response_too_large\"}\n");
    }
    return WebServer::send_json(request, detail_response_buffer);
}

static esp_err_t can_handler(WebServer::Request* request) {
    if (request->method == WebServer::Method::POST) {
        esp_err_t ret = WebServer::load_body(request);
        if (ret != ESP_OK) {
            return ret;
        }
        uint32_t baudrate = 0;
        uint32_t id = 0;
        if (json_get_uint32(request->body, "baudrate", &baudrate)) {
            if (baudrate == 0) {
                return WebServer::send(request, 400, "application/json", "{\"ok\":false,\"reason\":\"invalid_baudrate\"}\n", strlen("{\"ok\":false,\"reason\":\"invalid_baudrate\"}\n"));
            }
            CanCallback::CAN_BAUDRATE = baudrate;
        }
        if (json_get_uint32(request->body, "id", &id)) {
            CanCallback::CAN_ID = id;
        }
    }

    uint32_t can_id = CanCallback::CAN_ID;
    uint32_t baudrate = CanCallback::CAN_BAUDRATE;
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":true,\"baudrate\":%lu,\"id\":%lu,\"id_hex\":\"0x%lX\",\"note\":\"changed values may require CAN reinitialization or reboot\"}\n",
        baudrate,
        can_id,
        can_id);
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t calibration_handler(WebServer::Request* request) {
    auto params = CurrentCalib::params_data.read();
    float sample_resistance_mohm = params.current_base_K == 0 ? 0.0f : 2500.0f / params.current_base_K;
    size_t pos = 0;
    bool ok = append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos,
        "{\"current_base_k\":%u,\"sample_resistance_mohm\":%.3f,\"temperature_k\":%d,\"base_temperature_c\":%.2f,\"points\":[",
        static_cast<unsigned>(params.current_base_K),
        sample_resistance_mohm,
        params.temperature_K,
        CurrentCalib::BASE_TEMPERATURE / 100.0f);

    for (size_t i = 0; ok && i < sizeof(params.points) / sizeof(params.points[0]); ++i) {
        ok = append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos,
            "%s{\"index\":%u,\"register_value\":%d,\"no_offset_ma\":%d,\"offset_ua\":%d}",
            i == 0 ? "" : ",",
            static_cast<unsigned>(i),
            params.points[i].register_value,
            params.points[i].register_value * params.current_base_K / 1000,
            params.points[i].offset_current_100uA * 100);
    }

    if (!ok || !append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos, "]}\n")) {
        snprintf(detail_response_buffer, sizeof(detail_response_buffer), "{\"error\":\"response_too_large\"}\n");
    }
    return WebServer::send_json(request, detail_response_buffer);
}

static esp_err_t diagnostics_handler(WebServer::Request* request) {
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ina226\":{\"current_register_raw\":%d,\"voltage_register_raw\":%u,\"available\":%s}}\n",
        current_register_raw == nullptr ? 0 : *current_register_raw,
        voltage_register_raw == nullptr ? 0 : *voltage_register_raw,
        (current_register_raw != nullptr && voltage_register_raw != nullptr) ? "true" : "false");
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t wifi_status_handler(WebServer::Request* request) {
    IP_t ip = WifiService::get_ip();
    char ip_text[16] = {};
    char sta_mac[18] = {};
    char ap_mac[18] = {};
    ip_to_str(ip, ip_text, sizeof(ip_text));
    mac_to_str(WiFiManager::instance().get_mac(WIFI_IF_STA), sta_mac, sizeof(sta_mac));
    mac_to_str(WiFiManager::instance().get_mac(WIFI_IF_AP), ap_mac, sizeof(ap_mac));
    auto cfg = WifiService::get_config();
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"mode\":\"%s\",\"state\":%d,\"ip\":\"%s\",\"saved_ssid\":\"%s\",\"ap_ssid\":\"%s\",\"sta_mac\":\"%s\",\"ap_mac\":\"%s\",\"boot_enabled\":%s,\"last_error\":\"%s\"}\n",
        mode_to_str(WifiService::get_mode()),
        (int)WifiService::get_wifi_state(),
        ip_text,
        cfg.ssid,
        WifiService::get_ap_ssid(),
        sta_mac,
        ap_mac,
        cfg.web_enabled_on_boot ? "true" : "false",
        WifiService::get_last_error());
    return WebServer::send_json(request, response_buffer);
}

static const char* authmode_to_str(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN: return "open";
        case WIFI_AUTH_WEP: return "wep";
        case WIFI_AUTH_WPA_PSK: return "wpa";
        case WIFI_AUTH_WPA2_PSK: return "wpa2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "wpa_wpa2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2_enterprise";
        case WIFI_AUTH_WPA3_PSK: return "wpa3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2_wpa3";
        case WIFI_AUTH_WAPI_PSK: return "wapi";
        default: return "unknown";
    }
}

static size_t append_json_escaped(char* out, size_t out_size, size_t pos, const char* text) {
    if (pos >= out_size) {
        return pos;
    }
    for (const char* p = text; *p != '\0' && pos < out_size - 1; ++p) {
        if (*p == '"' || *p == '\\') {
            if (pos + 2 >= out_size) {
                break;
            }
            out[pos++] = '\\';
            out[pos++] = *p;
        } else if (static_cast<unsigned char>(*p) < 0x20) {
            if (pos + 6 >= out_size) {
                break;
            }
            int n = snprintf(out + pos, out_size - pos, "\\u%04x", static_cast<unsigned char>(*p));
            if (n < 0) {
                break;
            }
            pos += static_cast<size_t>(n);
        } else {
            out[pos++] = *p;
        }
    }
    out[pos] = '\0';
    return pos;
}

static bool append_checked(char* out, size_t out_size, size_t* pos, const char* fmt, ...) {
    if (out == nullptr || pos == nullptr || *pos >= out_size) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(out + *pos, out_size - *pos, fmt, args);
    va_end(args);

    if (n < 0 || static_cast<size_t>(n) >= out_size - *pos) {
        if (out_size > 0) {
            out[out_size - 1] = '\0';
        }
        return false;
    }
    *pos += static_cast<size_t>(n);
    return true;
}

static esp_err_t logs_api_handler(WebServer::Request* request) {
    char since_text[24] = {};
    uint64_t since = 0;
    if (WebServer::get_query_value(request, "since", since_text, sizeof(since_text)) == ESP_OK) {
        since = strtoull(since_text, nullptr, 10);
    }

    uint64_t from_seq = 0;
    uint64_t next_seq = 0;
    uint64_t latest_seq = 0;
    bool dropped = false;
    size_t len = read_log_ring(since, log_snapshot_buffer, sizeof(log_snapshot_buffer), &from_seq, &next_seq, &latest_seq, &dropped);

    size_t pos = 0;
    bool ok = append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos,
        "{\"from\":%" PRIu64 ",\"seq\":%" PRIu64 ",\"latest\":%" PRIu64 ",\"dropped\":%s,\"bytes\":%u,\"text\":\"",
        from_seq,
        next_seq,
        latest_seq,
        dropped ? "true" : "false",
        static_cast<unsigned>(len));
    if (ok) {
        pos = append_json_escaped(detail_response_buffer, sizeof(detail_response_buffer), pos, log_snapshot_buffer);
        ok = append_checked(detail_response_buffer, sizeof(detail_response_buffer), &pos, "\"}\n");
    }
    if (!ok) {
        snprintf(detail_response_buffer, sizeof(detail_response_buffer), "{\"error\":\"response_too_large\",\"seq\":%" PRIu64 "}\n", next_seq);
    }
    return WebServer::send_json(request, detail_response_buffer);
}

static esp_err_t logs_clear_handler(WebServer::Request* request) {
    clear_log_ring();
    return WebServer::send_json(request, "{\"ok\":true,\"seq\":0}\n");
}

static esp_err_t wifi_scan_handler(WebServer::Request* request) {
    WifiService::ScanResult results[WifiService::WIFI_SCAN_MAX_RESULTS] = {};
    size_t count = 0;
    esp_err_t ret = WifiService::scan_ap_list(results, WifiService::WIFI_SCAN_MAX_RESULTS, &count);
    if (ret != ESP_OK) {
        snprintf(scan_response_buffer, sizeof(scan_response_buffer),
            "{\"ok\":false,\"reason\":\"%s\",\"aps\":[]}\n", esp_err_to_name(ret));
        return WebServer::send_json(request, scan_response_buffer);
    }

    size_t pos = 0;
    bool ok = append_checked(scan_response_buffer, sizeof(scan_response_buffer), &pos,
        "{\"ok\":true,\"count\":%u,\"aps\":[", static_cast<unsigned>(count));

    for (size_t i = 0; ok && i < count && pos < sizeof(scan_response_buffer) - 1; ++i) {
        ok = append_checked(scan_response_buffer, sizeof(scan_response_buffer), &pos,
            "%s{\"ssid\":\"", i == 0 ? "" : ",");
        if (!ok) {
            break;
        }
        pos = append_json_escaped(scan_response_buffer, sizeof(scan_response_buffer), pos, results[i].ssid);
        ok = append_checked(scan_response_buffer, sizeof(scan_response_buffer), &pos,
            "\",\"rssi\":%d,\"channel\":%u,\"auth\":\"%s\",\"secure\":%s}",
            results[i].rssi,
            static_cast<unsigned>(results[i].channel),
            authmode_to_str(results[i].authmode),
            results[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }

    if (!ok || !append_checked(scan_response_buffer, sizeof(scan_response_buffer), &pos, "]}\n")) {
        snprintf(scan_response_buffer, sizeof(scan_response_buffer), "{\"ok\":false,\"reason\":\"response_too_large\",\"aps\":[]}\n");
    }
    return WebServer::send_json(request, scan_response_buffer);
}

static esp_err_t wifi_connect_handler(WebServer::Request* request) {
    esp_err_t ret = WebServer::load_body(request);
    if (ret != ESP_OK) {
        return ret;
    }

    char ssid[WIFI_SSID_MAX_LEN + 1] = {};
    char password[WIFI_PASSWORD_MAX_LEN + 1] = {};
    if (!json_get_string(request->body, "ssid", ssid, sizeof(ssid))) {
        return WebServer::send(request, 400, "application/json", "{\"ok\":false,\"reason\":\"missing_ssid\"}\n", strlen("{\"ok\":false,\"reason\":\"missing_ssid\"}\n"));
    }
    json_get_string(request->body, "password", password, sizeof(password));

    ret = WifiService::connect_sta(ssid, password, true);
    if (ret != ESP_OK) {
        WifiService::start_provision_ap();
    }

    IP_t ip = WifiService::get_ip();
    char ip_text[16] = {};
    ip_to_str(ip, ip_text, sizeof(ip_text));
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\",\"ip\":\"%s\",\"mode\":\"%s\"}\n",
        ret == ESP_OK ? "true" : "false",
        ret == ESP_OK ? "ok" : esp_err_to_name(ret),
        ip_text,
        mode_to_str(WifiService::get_mode()));
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t wifi_ap_handler(WebServer::Request* request) {
    esp_err_t ret = WifiService::start_provision_ap();
    IP_t ip = WifiService::get_ip();
    char ip_text[16] = {};
    ip_to_str(ip, ip_text, sizeof(ip_text));
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\",\"ap_ssid\":\"%s\",\"ip\":\"%s\"}\n",
        ret == ESP_OK ? "true" : "false",
        ret == ESP_OK ? "ok" : esp_err_to_name(ret),
        WifiService::get_ap_ssid(),
        ip_text);
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t wifi_off_handler(WebServer::Request* request) {
    esp_err_t ret = WifiService::stop();
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\"}\n",
        ret == ESP_OK ? "true" : "false",
        ret == ESP_OK ? "ok" : esp_err_to_name(ret));
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t wifi_on_handler(WebServer::Request* request) {
    esp_err_t ret = WifiService::start_default();
    IP_t ip = WifiService::get_ip();
    char ip_text[16] = {};
    ip_to_str(ip, ip_text, sizeof(ip_text));
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\",\"mode\":\"%s\",\"ip\":\"%s\"}\n",
        ret == ESP_OK ? "true" : "false",
        ret == ESP_OK ? "ok" : esp_err_to_name(ret),
        mode_to_str(WifiService::get_mode()),
        ip_text);
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t wifi_boot_handler(WebServer::Request* request) {
    esp_err_t ret = WebServer::load_body(request);
    if (ret != ESP_OK) {
        return ret;
    }
    bool enabled = true;
    if (!json_get_bool(request->body, "enabled", &enabled)) {
        return WebServer::send(request, 400, "application/json", "{\"ok\":false,\"reason\":\"missing_enabled\"}\n", strlen("{\"ok\":false,\"reason\":\"missing_enabled\"}\n"));
    }
    ret = WifiService::set_web_enabled_on_boot(enabled);
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\",\"boot_enabled\":%s}\n",
        ret == ESP_OK ? "true" : "false",
        ret == ESP_OK ? "ok" : esp_err_to_name(ret),
        WifiService::is_web_enabled_on_boot() ? "true" : "false");
    return WebServer::send_json(request, response_buffer);
}

static esp_err_t wifi_clear_handler(WebServer::Request* request) {
    esp_err_t ret = WifiService::clear_saved_sta();
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"ok\":%s,\"reason\":\"%s\"}\n",
        ret == ESP_OK ? "true" : "false",
        ret == ESP_OK ? "ok" : esp_err_to_name(ret));
    return WebServer::send_json(request, response_buffer);
}

esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    install_log_capture();

    ESP_ERROR_CHECK(WebServer::init(80));
    ESP_ERROR_CHECK(WebServer::use(cors_middleware));
    ESP_ERROR_CHECK(WebServer::use(log_middleware));
    ESP_ERROR_CHECK(WebServer::on("/", WebServer::Method::GET, index_handler));
    ESP_ERROR_CHECK(WebServer::on("/index.html", WebServer::Method::GET, main_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/charts.html", WebServer::Method::GET, charts_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/control.html", WebServer::Method::GET, control_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/status.html", WebServer::Method::GET, status_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/logs.html", WebServer::Method::GET, logs_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/blackbox.html", WebServer::Method::GET, blackbox_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/app.css", WebServer::Method::GET, app_css_handler));
    ESP_ERROR_CHECK(WebServer::on("/provision", WebServer::Method::GET, provision_handler));
    ESP_ERROR_CHECK(WebServer::on("/provision.html", WebServer::Method::GET, provision_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/state", WebServer::Method::GET, state_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/output", WebServer::Method::POST, output_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/reboot", WebServer::Method::POST, reboot_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/system", WebServer::Method::GET, system_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/backlight", WebServer::Method::GET, backlight_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/backlight", WebServer::Method::POST, backlight_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/protect", WebServer::Method::GET, protect_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/protect", WebServer::Method::POST, protect_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/can", WebServer::Method::GET, can_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/can", WebServer::Method::POST, can_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/calibration", WebServer::Method::GET, calibration_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/diagnostics", WebServer::Method::GET, diagnostics_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/logs", WebServer::Method::GET, logs_api_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/logs/clear", WebServer::Method::POST, logs_clear_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/status", WebServer::Method::GET, wifi_status_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/scan", WebServer::Method::GET, wifi_scan_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/on", WebServer::Method::POST, wifi_on_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/connect", WebServer::Method::POST, wifi_connect_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/ap", WebServer::Method::POST, wifi_ap_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/off", WebServer::Method::POST, wifi_off_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/boot", WebServer::Method::POST, wifi_boot_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/clear", WebServer::Method::POST, wifi_clear_handler));

    WebServer::enable_captive_portal(false);
    WebServer::on_not_found([](WebServer::Request* request) -> esp_err_t {
        if (WifiService::is_provisioning()) {
            return WebServer::send_html(request, provision_html_file.data, provision_html_file.size);
        }
        return WebServer::send(request, 404, "application/json", "{\"error\":\"not found\"}\n", strlen("{\"error\":\"not found\"}\n"));
    });

    initialized = true;
    ESP_LOGI(TAG, "routes registered");
    return ESP_OK;
}

esp_err_t start() {
    if (!initialized) {
        ESP_ERROR_CHECK(init());
    }
    esp_err_t ret = WebServer::begin();
    if (ret == ESP_OK) {
        running = true;
    }
    return ret;
}

esp_err_t start_with_wifi_service() {
    if (!WifiService::is_web_enabled_on_boot()) {
        ESP_LOGI(TAG, "Web/WiFi startup disabled by NVS");
        return ESP_OK;
    }

    ESP_ERROR_CHECK(init());

    esp_err_t wifi_ret = WifiService::start_default();
    if (wifi_ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi service start failed: %s", esp_err_to_name(wifi_ret));
    }

    esp_err_t web_ret = start();
    if (web_ret != ESP_OK) {
        ESP_LOGE(TAG, "Web backend start failed: %s", esp_err_to_name(web_ret));
    }

    IP_t ip = WifiService::get_ip();
    ESP_LOGI(TAG, "Web active, open http://%u.%u.%u.%u/", ip.octet1, ip.octet2, ip.octet3, ip.octet4);

    if (web_ret != ESP_OK) {
        return web_ret;
    }
    return wifi_ret;
}

esp_err_t stop() {
    running = false;
    return WebServer::stop();
}

bool is_running() {
    return running;
}

} // namespace WebBackend
