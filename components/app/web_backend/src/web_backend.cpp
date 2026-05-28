#include "web_backend.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "esp_log.h"
#include "esp_timer.h"
#include "global_state.h"
#include "power_output.h"
#include "protect.h"
#include "web_file.h"
#include "web_server.h"
#include "wifi_service.h"

namespace WebBackend {

static const char* TAG = "WebBackend";
static bool initialized = false;
static bool running = false;
static char response_buffer[1536];

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

static esp_err_t wifi_status_handler(WebServer::Request* request) {
    IP_t ip = WifiService::get_ip();
    char ip_text[16] = {};
    ip_to_str(ip, ip_text, sizeof(ip_text));
    auto cfg = WifiService::get_config();
    snprintf(response_buffer, sizeof(response_buffer),
        "{\"mode\":\"%s\",\"state\":%d,\"ip\":\"%s\",\"saved_ssid\":\"%s\",\"ap_ssid\":\"%s\",\"boot_enabled\":%s,\"last_error\":\"%s\"}\n",
        mode_to_str(WifiService::get_mode()),
        (int)WifiService::get_wifi_state(),
        ip_text,
        cfg.ssid,
        WifiService::get_ap_ssid(),
        cfg.web_enabled_on_boot ? "true" : "false",
        WifiService::get_last_error());
    return WebServer::send_json(request, response_buffer);
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

esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(WebServer::init(80));
    ESP_ERROR_CHECK(WebServer::use(cors_middleware));
    ESP_ERROR_CHECK(WebServer::use(log_middleware));
    ESP_ERROR_CHECK(WebServer::on("/", WebServer::Method::GET, index_handler));
    ESP_ERROR_CHECK(WebServer::on("/index.html", WebServer::Method::GET, main_page_handler));
    ESP_ERROR_CHECK(WebServer::on("/provision", WebServer::Method::GET, provision_handler));
    ESP_ERROR_CHECK(WebServer::on("/provision.html", WebServer::Method::GET, provision_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/state", WebServer::Method::GET, state_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/output", WebServer::Method::POST, output_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/status", WebServer::Method::GET, wifi_status_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/connect", WebServer::Method::POST, wifi_connect_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/ap", WebServer::Method::POST, wifi_ap_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/wifi/off", WebServer::Method::POST, wifi_off_handler));

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
    ESP_ERROR_CHECK(init());

    if (!WifiService::is_web_enabled_on_boot()) {
        ESP_LOGI(TAG, "Web/WiFi startup disabled by NVS");
        return ESP_OK;
    }

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
