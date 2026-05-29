/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: Web 后端静态页面响应处理
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-29
 */
#include "web_backend_internal.h"

#include "web_file.h"
#include "wifi_service.h"

namespace WebBackend {

/**
 * @brief 根路径入口。
 *
 * STA 模式返回主页面；AP 配网模式返回配网页。
 * 这样用户连上设备热点后访问任意入口都能进入配网流程。
 */
esp_err_t index_handler(WebServer::Request* request) {
    if (WifiService::is_provisioning()) {
        return WebServer::send_html(request, provision_html_file.data, provision_html_file.size);
    }
    return WebServer::send_html(request, index_html_file.data, index_html_file.size);
}

/** @brief 返回实时概览页面。 */
esp_err_t main_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, index_html_file.data, index_html_file.size);
}

/** @brief 返回趋势曲线页面。 */
esp_err_t charts_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, charts_html_file.data, charts_html_file.size);
}

/** @brief 返回控制设置页面。 */
esp_err_t control_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, control_html_file.data, control_html_file.size);
}

/** @brief 返回状态诊断页面。 */
esp_err_t status_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, status_html_file.data, status_html_file.size);
}

/** @brief 返回实时日志页面。 */
esp_err_t logs_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, logs_html_file.data, logs_html_file.size);
}

/** @brief 返回黑匣子日志入口页面。 */
esp_err_t blackbox_page_handler(WebServer::Request* request) {
    return WebServer::send_html(request, blackbox_html_file.data, blackbox_html_file.size);
}

/** @brief 返回 Web 公共样式表。 */
esp_err_t app_css_handler(WebServer::Request* request) {
    return WebServer::send(request, 200, "text/css", app_css_file.data, app_css_file.size);
}

/** @brief 返回 AP 配网页。 */
esp_err_t provision_handler(WebServer::Request* request) {
    return WebServer::send_html(request, provision_html_file.data, provision_html_file.size);
}


} // namespace WebBackend
