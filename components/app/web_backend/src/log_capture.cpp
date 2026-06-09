/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: Web 实时日志捕获与 RAM 环形缓冲
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-29
 */
#include "web_backend_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "diagnostic_log.h"

namespace WebBackend {

/*
 * 实时日志只保存在 RAM 中，不写 Flash。
 * 这样可以让 Web 页面看到最近日志，同时避免频繁写入影响 Flash 寿命。
 */
static constexpr size_t LOG_RING_SIZE = 8 * 1024;
static constexpr size_t LOG_LINE_MAX_LEN = 256;
static char log_ring[LOG_RING_SIZE];
static portMUX_TYPE log_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static size_t log_ring_write_pos = 0;
static size_t log_ring_used = 0;
static uint64_t log_ring_seq = 0;
static vprintf_like_t original_log_vprintf = nullptr;
static bool log_capture_installed = false;

static const char* method_to_str(WebServer::Method method) {
    switch (method) {
        case WebServer::Method::GET: return "GET";
        case WebServer::Method::POST: return "POST";
        case WebServer::Method::PUT: return "PUT";
        case WebServer::Method::DELETE_: return "DELETE";
        case WebServer::Method::PATCH: return "PATCH";
        case WebServer::Method::OPTIONS: return "OPTIONS";
        case WebServer::Method::HEAD: return "HEAD";
        default: return "ANY";
    }
}

/** @brief 将原始日志字节写入环形缓冲，满后覆盖最旧数据。 */
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

/**
 * @brief ESP_LOG vprintf 钩子。
 *
 * ESP-IDF 日志最终会调用一个 vprintf 风格函数。这里先转发给原始输出，
 * 再截取同一行写入 RAM 环形缓冲，保证串口日志和 Web 日志同时可用。
 */
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

/** @brief 安装日志捕获钩子，重复调用不会重复安装。 */
void install_log_capture() {
    if (log_capture_installed) {
        return;
    }
    original_log_vprintf = esp_log_set_vprintf(log_capture_vprintf);
    log_capture_installed = true;
}

/** @brief 清空 Web 实时日志缓冲区。 */
void clear_log_ring() {
    portENTER_CRITICAL(&log_ring_lock);
    log_ring_write_pos = 0;
    log_ring_used = 0;
    log_ring_seq = 0;
    portEXIT_CRITICAL(&log_ring_lock);
}

/**
 * @brief 从环形缓冲读取 since 之后的日志内容。
 *
 * Web 前端轮询 `/api/logs?since=<seq>` 时使用增量读取。
 * 若客户端太久没读，旧日志已被覆盖，dropped 会被置为 true。
 */
size_t read_log_ring(uint64_t since, char* out, size_t out_size, uint64_t* from_seq, uint64_t* next_seq, uint64_t* latest_seq, bool* dropped) {
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


/** @brief 请求日志中间件，实时日志接口本身不再记录以避免递归刷屏。 */
esp_err_t log_middleware(WebServer::Request* request) {
    if (strcmp(request->uri, "/api/logs") == 0 || strcmp(request->uri, "/api/logs/clear") == 0) {
        return ESP_OK;
    }
    const char* method = method_to_str(request->method);
    ESP_LOGI("WebBackend", "request ip=%s method=%s uri=%s",
             request->peer_ip, method, request->uri);
    if (request->method != WebServer::Method::GET &&
        request->method != WebServer::Method::HEAD &&
        request->method != WebServer::Method::OPTIONS) {
        DEVICE_EVENT_I("WebBackend", "web: action ip=%s method=%s uri=%s",
                       request->peer_ip, method, request->uri);
    }
    return ESP_OK;
}

} // namespace WebBackend
