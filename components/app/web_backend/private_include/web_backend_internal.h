/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: Web 后端组件内部接口，仅供 web_backend 组件内多个源文件共享
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-29
 */
#ifndef WEB_BACKEND_INTERNAL_H
#define WEB_BACKEND_INTERNAL_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_partition.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "wifi_service.h"

namespace WebBackend {

/*
 * 这些响应缓冲区定义在 web_backend.cpp 中，其他 handler 只引用。
 * 嵌入式 Web 后端优先使用固定静态缓冲，避免每个 HTTP 请求动态分配堆内存。
 */
extern char response_buffer[1536];
extern char detail_response_buffer[4096];
extern char scan_response_buffer[3072];

/** @brief 安装 ESP_LOG 捕获钩子，将实时日志写入 RAM 环形缓冲。 */
void install_log_capture();

/** @brief 清空实时日志 RAM 环形缓冲。 */
void clear_log_ring();

/**
 * @brief 按日志序号增量读取 RAM 环形日志。
 *
 * @param since 客户端已读到的序号，服务端从该序号之后返回数据
 * @param out 输出缓冲区
 * @param out_size 输出缓冲区长度
 * @param from_seq 实际读取起始序号
 * @param next_seq 下次读取应传入的序号
 * @param latest_seq 当前最新日志序号
 * @param dropped true 表示 since 早于环形缓冲保留范围，旧日志已被覆盖
 * @return 实际复制到 out 的字节数
 */
size_t read_log_ring(uint64_t since, char* out, size_t out_size, uint64_t* from_seq, uint64_t* next_seq, uint64_t* latest_seq, bool* dropped);

/** @brief 从 JSON 请求体中读取字符串字段，结果复制到调用方提供的固定缓冲。 */
bool json_get_string(const char* json, const char* key, char* out, size_t out_size);

/** @brief 从 JSON 请求体中读取布尔字段。 */
bool json_get_bool(const char* json, const char* key, bool* out);

/** @brief 从 JSON 请求体中读取 uint32 字段。 */
bool json_get_uint32(const char* json, const char* key, uint32_t* out);

/** @brief 判断 JSON 请求体中是否存在指定字段。 */
bool json_has_key(const char* json, const char* key);

/** @brief 将 WiFiService 模式转换为前端 API 使用的短字符串。 */
const char* mode_to_str(WifiService::Mode mode);

/** @brief 将 IP_t 转换为点分十进制字符串。 */
void ip_to_str(IP_t ip, char* out, size_t out_size);

/*
 * WebServer::Middleware 会在路由 handler 之前执行，适合做日志、鉴权、CORS 等横切逻辑。
 * 当前组件只做请求日志，CORS 放在 web_backend.cpp 内，便于和路由注册一起阅读。
 */
esp_err_t log_middleware(WebServer::Request* request);

/* 页面 handler：只负责从固件内嵌资源返回 HTML/CSS。 */
esp_err_t index_handler(WebServer::Request* request);
esp_err_t main_page_handler(WebServer::Request* request);
esp_err_t charts_page_handler(WebServer::Request* request);
esp_err_t control_page_handler(WebServer::Request* request);
esp_err_t status_page_handler(WebServer::Request* request);
esp_err_t logs_page_handler(WebServer::Request* request);
esp_err_t blackbox_page_handler(WebServer::Request* request);
esp_err_t firmware_page_handler(WebServer::Request* request);
esp_err_t app_css_handler(WebServer::Request* request);
esp_err_t provision_handler(WebServer::Request* request);

/* API handler：负责解析 HTTP 请求、调用业务模块、返回 JSON。 */
esp_err_t state_handler(WebServer::Request* request);
esp_err_t output_handler(WebServer::Request* request);
esp_err_t meter_reset_handler(WebServer::Request* request);
esp_err_t reboot_handler(WebServer::Request* request);
esp_err_t system_handler(WebServer::Request* request);
esp_err_t backlight_handler(WebServer::Request* request);
esp_err_t start_logo_handler(WebServer::Request* request);
esp_err_t protect_handler(WebServer::Request* request);
esp_err_t can_handler(WebServer::Request* request);
esp_err_t calibration_handler(WebServer::Request* request);
esp_err_t diagnostics_handler(WebServer::Request* request);
esp_err_t rtos_stats_handler(WebServer::Request* request);
esp_err_t wifi_status_handler(WebServer::Request* request);
esp_err_t logs_api_handler(WebServer::Request* request);
esp_err_t logs_clear_handler(WebServer::Request* request);
esp_err_t blackbox_api_handler(WebServer::Request* request);
esp_err_t blackbox_clear_handler(WebServer::Request* request);
esp_err_t blackbox_config_handler(WebServer::Request* request);
esp_err_t wifi_scan_handler(WebServer::Request* request);
esp_err_t wifi_connect_handler(WebServer::Request* request);
esp_err_t wifi_ap_handler(WebServer::Request* request);
esp_err_t wifi_off_handler(WebServer::Request* request);
esp_err_t wifi_on_handler(WebServer::Request* request);
esp_err_t wifi_boot_handler(WebServer::Request* request);
esp_err_t wifi_clear_handler(WebServer::Request* request);
esp_err_t ota_status_handler(WebServer::Request* request);
esp_err_t ota_upload_handler(WebServer::Request* request);
esp_err_t ota_activate_handler(WebServer::Request* request);
esp_err_t ota_abort_handler(WebServer::Request* request);
esp_err_t ota_remote_check_handler(WebServer::Request* request);
esp_err_t ota_remote_download_handler(WebServer::Request* request);

/** @brief 将 OTA APP 分区转换为用户可读槽位编号，未知时返回 0。 */
uint8_t ota_partition_slot(const esp_partition_t* partition);

} // namespace WebBackend

#endif
