/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: Web 后端生命周期与路由注册入口
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-30 00:38:06
 */
#include "web_backend.h"
#include "web_backend_internal.h"

#include <cstring>

#include "esp_log.h"
#include "esp_system.h"
#include "web_file.h"
#include "web_server.h"
#include "wifi_service.h"

namespace WebBackend {

static const char* TAG = "WebBackend";
static bool initialized = false;
static bool running = false;

/*
 * 统一响应缓冲区。
 * ESP32-C6 RAM 有限，Web 后端避免为每次请求创建 std::string/json DOM。
 * handler 按响应规模选择缓冲区，发送完成后内容即可被下一次请求覆盖。
 */
char response_buffer[1536];
char detail_response_buffer[4096];
char scan_response_buffer[3072];

/**
 * @brief 设置 CORS 响应头并处理 OPTIONS 预检请求。
 *
 * CORS 是浏览器安全策略的一部分。若前端页面与 API 不在同一来源，
 * 浏览器会先发 OPTIONS 预检请求确认允许的方法和 Header。
 * 设备 Web UI 通常同源访问，但保留 CORS 便于调试工具或局域网页面调用。
 */
static esp_err_t cors_middleware(WebServer::Request* request) {
    httpd_resp_set_hdr(request->raw, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(request->raw, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(request->raw, "Access-Control-Allow-Headers", "Content-Type");

    if (request->method == WebServer::Method::OPTIONS) {
        return WebServer::send(request, 204, "text/plain", "", 0);
    }
    return ESP_OK;
}

/**
 * @brief 初始化 Web 后端并注册所有页面与 API 路由。
 *
 * 路由注册只执行一次。WebServer 当前要求 begin() 前完成路由注册，
 * 因此新增 API 时应在这里集中登记，便于维护接口契约。
 */
esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    install_log_capture();

    ESP_ERROR_CHECK(WebServer::init(80));
    ESP_ERROR_CHECK(WebServer::use(cors_middleware));
    ESP_ERROR_CHECK(WebServer::use(log_middleware));

    /* 页面路由：直接返回固件内嵌 HTML/CSS 资源。 */
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

    /* REST API 路由：返回 JSON，POST 路由通过请求体携带参数。 */
    ESP_ERROR_CHECK(WebServer::on("/api/state", WebServer::Method::GET, state_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/output", WebServer::Method::POST, output_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/meter/reset", WebServer::Method::POST, meter_reset_handler));
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
    ESP_ERROR_CHECK(WebServer::on("/api/rtos/stats", WebServer::Method::GET, rtos_stats_handler));
    ESP_ERROR_CHECK(WebServer::on("/api/rtos/stats", WebServer::Method::POST, rtos_stats_handler));
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
        /*
         * AP 配网模式下，手机/电脑可能访问任意探测 URL 判断网络是否可上网。
         * Captive Portal 的常见做法是把未匹配路径回落到配网页，方便用户完成配网。
         */
        if (WifiService::is_provisioning()) {
            return WebServer::send_html(request, provision_html_file.data, provision_html_file.size);
        }
        return WebServer::send(request, 404, "application/json", "{\"error\":\"not found\"}\n", strlen("{\"error\":\"not found\"}\n"));
    });

    initialized = true;
    ESP_LOGI(TAG, "routes registered");
    return ESP_OK;
}

/** @brief 启动底层 HTTP 服务。 */
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

/**
 * @brief 按 WiFiService 的 NVS 配置启动 WiFi 和 Web 后端。
 *
 * 启动顺序：先注册 Web 路由，再启动 WiFi 默认模式，最后启动 HTTP 服务。
 * WiFi 失败不会阻止返回 Web 错误，便于日志中同时看到网络和 HTTP 的状态。
 */
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

/** @brief 停止 HTTP 服务并更新运行标志。 */
esp_err_t stop() {
    running = false;
    return WebServer::stop();
}

/** @brief 查询 Web 后端运行状态。 */
bool is_running() {
    return running;
}

} // namespace WebBackend
