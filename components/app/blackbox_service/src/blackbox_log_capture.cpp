/*
 * @Description: ESP_LOG 捕获、解析和固定 RAM 事件环
 */
#include "blackbox_service_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log_write.h"
#include "freertos/FreeRTOS.h"

namespace BlackboxService {
namespace Internal {
namespace {

constexpr size_t LOG_LINE_BUFFER_SIZE = 256;
constexpr size_t LOG_EVENT_RING_SIZE = 32;

portMUX_TYPE event_ring_lock = portMUX_INITIALIZER_UNLOCKED;
LogEvent event_ring[LOG_EVENT_RING_SIZE];
size_t event_ring_read = 0;
size_t event_ring_write = 0;
size_t event_ring_used = 0;
vprintf_like_t previous_log_vprintf = nullptr;
bool capture_busy = false;
char capture_line[LOG_LINE_BUFFER_SIZE];
char clean_line[LOG_LINE_BUFFER_SIZE];
LogEvent capture_event;

bool is_internal_tag(const char* tag, size_t len) {
    constexpr const char* INTERNAL_TAGS[] = {
        "Blackbox",
        "BlackBox",
        "CircularFlashBuffer",
    };
    for (const char* internal : INTERNAL_TAGS) {
        if (strlen(internal) == len && strncmp(tag, internal, len) == 0) {
            return true;
        }
    }
    return false;
}

bool is_ignored_tag(const char* tag, size_t len) {
    constexpr const char* IGNORED_TAGS[] = {
        "wifi",
        "wifi_init",
        "phy_init",
        "pp",
        "net80211",
        "WebBackend",
    };
    for (const char* ignored : IGNORED_TAGS) {
        if (strlen(ignored) == len && strncmp(tag, ignored, len) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief 移除 ANSI 控制序列，便于将串口日志转成持久化文本。 */
void strip_ansi(const char* input, char* output, size_t output_size) {
    size_t out = 0;
    for (size_t i = 0; input[i] != '\0' && out + 1 < output_size;) {
        if (input[i] == '\x1b' && input[i + 1] == '[') {
            i += 2;
            while (input[i] != '\0' && (input[i] < '@' || input[i] > '~')) {
                ++i;
            }
            if (input[i] != '\0') {
                ++i;
            }
            continue;
        }
        output[out++] = input[i++];
    }
    output[out] = '\0';
}

/**
 * @brief 将 ESP-IDF Log V1 格式转换为 [L][TAG] message。
 *
 * 当前 sdkconfig 使用 CONFIG_LOG_VERSION_1。该模式会将完整日志行一次性交给
 * vprintf 钩子。若将来切换到 Log V2，需要相应调整解析逻辑。
 */
bool format_log_event(const char* raw, LogEvent* event) {
    strip_ansi(raw, clean_line, sizeof(clean_line));

    const char level = clean_line[0];
    if (level != 'I' && level != 'W' && level != 'E') {
        return false;
    }

    const char* tag = strstr(clean_line, ") ");
    if (tag == nullptr) {
        return false;
    }
    tag += 2;

    const char* message = strstr(tag, ": ");
    if (message == nullptr || message == tag) {
        return false;
    }
    const size_t tag_len = static_cast<size_t>(message - tag);
    if (is_internal_tag(tag, tag_len) || is_ignored_tag(tag, tag_len)) {
        return false;
    }
    message += 2;

    size_t message_len = strlen(message);
    while (message_len > 0 && (message[message_len - 1] == '\r' || message[message_len - 1] == '\n')) {
        --message_len;
    }

    event->include_text = level == 'W' || level == 'E';
    if (event->include_text) {
        snprintf(event->text,
                 sizeof(event->text),
                 "[%c][%.*s] %.*s",
                 level,
                 static_cast<int>(tag_len),
                 tag,
                 static_cast<int>(message_len),
                 message);
    } else {
        event->text[0] = '\0';
    }
    return true;
}

/** @brief 钩子上下文只写固定 RAM 环，避免在日志调用链中调度或操作 Flash。 */
void push_log_event(const LogEvent& event) {
    portENTER_CRITICAL(&event_ring_lock);
    if (event_ring_used < LOG_EVENT_RING_SIZE) {
        event_ring[event_ring_write] = event;
        event_ring_write = (event_ring_write + 1) % LOG_EVENT_RING_SIZE;
        ++event_ring_used;
    }
    portEXIT_CRITICAL(&event_ring_lock);
}

/** @brief 全局 ESP_LOG vprintf 钩子。先保留原有输出，再投递黑匣子事件。 */
int blackbox_log_vprintf(const char* fmt, va_list args) {
    int ret = 0;
    if (previous_log_vprintf != nullptr) {
        va_list output_args;
        va_copy(output_args, args);
        ret = previous_log_vprintf(fmt, output_args);
        va_end(output_args);
    }

    if (__atomic_test_and_set(&capture_busy, __ATOMIC_ACQUIRE)) {
        return ret;
    }

    va_list capture_args;
    va_copy(capture_args, args);
    const int len = vsnprintf(capture_line, sizeof(capture_line), fmt, capture_args);
    va_end(capture_args);
    if (len <= 0) {
        __atomic_clear(&capture_busy, __ATOMIC_RELEASE);
        return ret;
    }

    capture_event = {};
    if (format_log_event(capture_line, &capture_event)) {
        push_log_event(capture_event);
    }
    __atomic_clear(&capture_busy, __ATOMIC_RELEASE);
    return ret;
}

} // namespace

void install_log_capture() {
    previous_log_vprintf = esp_log_set_vprintf(blackbox_log_vprintf);
}

bool pop_log_event(LogEvent* event) {
    portENTER_CRITICAL(&event_ring_lock);
    if (event_ring_used == 0) {
        portEXIT_CRITICAL(&event_ring_lock);
        return false;
    }
    *event = event_ring[event_ring_read];
    event_ring_read = (event_ring_read + 1) % LOG_EVENT_RING_SIZE;
    --event_ring_used;
    portEXIT_CRITICAL(&event_ring_lock);
    return true;
}

} // namespace Internal
} // namespace BlackboxService
