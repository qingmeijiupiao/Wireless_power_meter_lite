/*
 * @Description: ESP_LOG 捕获、解析和固定 RAM 事件环
 */
#include "blackbox_service_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "diagnostic_log.h"
#include "esp_log_write.h"
#include "freertos/FreeRTOS.h"

namespace BlackboxService {
namespace Internal {
namespace {

constexpr size_t LOG_LINE_BUFFER_SIZE = 256;
constexpr size_t LOG_EVENT_RING_SIZE = 32;
constexpr size_t CAPTURE_SLOT_COUNT = 4;

struct CaptureSlot {
    bool busy;
    char raw[LOG_LINE_BUFFER_SIZE];
    char clean[LOG_LINE_BUFFER_SIZE];
    char output[LOG_LINE_BUFFER_SIZE];
    LogEvent event;
};

portMUX_TYPE event_ring_lock = portMUX_INITIALIZER_UNLOCKED;
LogEvent event_ring[LOG_EVENT_RING_SIZE];
size_t event_ring_read = 0;
size_t event_ring_write = 0;
size_t event_ring_used = 0;
vprintf_like_t previous_log_vprintf = nullptr;
CaptureSlot capture_slots[CAPTURE_SLOT_COUNT] = {};
uint32_t dropped_no_slot = 0;
uint32_t dropped_ring_full = 0;
uint32_t dropped_parse_failed = 0;

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

void increment_counter(uint32_t* counter) {
    portENTER_CRITICAL(&event_ring_lock);
    ++(*counter);
    portEXIT_CRITICAL(&event_ring_lock);
}

CaptureSlot* acquire_slot() {
    for (auto& slot : capture_slots) {
        if (!__atomic_test_and_set(&slot.busy, __ATOMIC_ACQUIRE)) {
            return &slot;
        }
    }
    increment_counter(&dropped_no_slot);
    return nullptr;
}

void release_slot(CaptureSlot* slot) {
    __atomic_clear(&slot->busy, __ATOMIC_RELEASE);
}

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

PersistPolicy marker_policy(const char** message) {
    constexpr size_t TEXT_MARKER_LEN = sizeof(DIAGNOSTIC_LOG_TEXT_MARKER) - 1;
    constexpr size_t SNAPSHOT_MARKER_LEN = sizeof(DIAGNOSTIC_LOG_SNAPSHOT_MARKER) - 1;
    if (strncmp(*message, DIAGNOSTIC_LOG_TEXT_MARKER, TEXT_MARKER_LEN) == 0) {
        *message += TEXT_MARKER_LEN;
        return PersistPolicy::TEXT;
    }
    if (strncmp(*message, DIAGNOSTIC_LOG_SNAPSHOT_MARKER, SNAPSHOT_MARKER_LEN) == 0) {
        *message += SNAPSHOT_MARKER_LEN;
        return PersistPolicy::TEXT_AND_SNAPSHOT;
    }
    return PersistPolicy::NONE;
}

bool remove_marker_from_output(const char* raw, char* output, size_t output_size) {
    const char* marker = DIAGNOSTIC_LOG_TEXT_MARKER;
    const char* marker_pos = strstr(raw, marker);
    if (marker_pos == nullptr) {
        marker = DIAGNOSTIC_LOG_SNAPSHOT_MARKER;
        marker_pos = strstr(raw, marker);
    }
    if (marker_pos == nullptr) {
        return false;
    }
    const size_t prefix_len = static_cast<size_t>(marker_pos - raw);
    if (prefix_len >= output_size) {
        return false;
    }
    memcpy(output, raw, prefix_len);
    snprintf(output + prefix_len, output_size - prefix_len, "%s", marker_pos + strlen(marker));
    return true;
}

bool format_log_event(const char* raw, CaptureSlot* slot, bool* marked) {
    strip_ansi(raw, slot->clean, sizeof(slot->clean));

    const char level = slot->clean[0];
    if (level != 'I' && level != 'W' && level != 'E') {
        return false;
    }

    const char* tag = strstr(slot->clean, ") ");
    if (tag == nullptr) {
        return false;
    }
    tag += 2;

    const char* message = strstr(tag, ": ");
    if (message == nullptr || message == tag) {
        return false;
    }
    const size_t tag_len = static_cast<size_t>(message - tag);
    if (is_internal_tag(tag, tag_len)) {
        return false;
    }
    message += 2;

    PersistPolicy policy = marker_policy(&message);
    *marked = policy != PersistPolicy::NONE;
    if (level == 'W' || level == 'E') {
        policy = PersistPolicy::TEXT_AND_SNAPSHOT;
    } else if (policy == PersistPolicy::NONE) {
        return false;
    }

    size_t message_len = strlen(message);
    while (message_len > 0 && (message[message_len - 1] == '\r' || message[message_len - 1] == '\n')) {
        --message_len;
    }

    slot->event.policy = policy;
    snprintf(slot->event.text,
             sizeof(slot->event.text),
             "[%c][%.*s] %.*s",
             level,
             static_cast<int>(tag_len),
             tag,
             static_cast<int>(message_len),
             message);
    return true;
}

void push_log_event(const LogEvent& event) {
    portENTER_CRITICAL(&event_ring_lock);
    if (event_ring_used < LOG_EVENT_RING_SIZE) {
        event_ring[event_ring_write] = event;
        event_ring_write = (event_ring_write + 1) % LOG_EVENT_RING_SIZE;
        ++event_ring_used;
    } else {
        ++dropped_ring_full;
    }
    portEXIT_CRITICAL(&event_ring_lock);
}

int output_clean_line(const char* line) {
    if (previous_log_vprintf == nullptr) {
        return 0;
    }
    auto forward = [](vprintf_like_t output, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        const int result = output(fmt, args);
        va_end(args);
        return result;
    };
    return forward(previous_log_vprintf, "%s", line);
}

int output_original(const char* fmt, va_list args) {
    if (previous_log_vprintf == nullptr) {
        return 0;
    }
    va_list output_args;
    va_copy(output_args, args);
    const int ret = previous_log_vprintf(fmt, output_args);
    va_end(output_args);
    return ret;
}

int blackbox_log_vprintf(const char* fmt, va_list args) {
    CaptureSlot* slot = acquire_slot();
    if (slot == nullptr) {
        return output_original(fmt, args);
    }

    va_list capture_args;
    va_copy(capture_args, args);
    const int len = vsnprintf(slot->raw, sizeof(slot->raw), fmt, capture_args);
    va_end(capture_args);
    if (len <= 0) {
        increment_counter(&dropped_parse_failed);
        release_slot(slot);
        return output_original(fmt, args);
    }

    slot->event = {};
    bool marked = false;
    const bool captured = format_log_event(slot->raw, slot, &marked);

    int ret = 0;
    if (marked &&
        remove_marker_from_output(slot->raw, slot->output, sizeof(slot->output))) {
        ret = output_clean_line(slot->output);
    } else {
        ret = output_original(fmt, args);
    }

    if (captured) {
        push_log_event(slot->event);
    }
    release_slot(slot);
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

CaptureDropStats take_capture_drop_stats() {
    portENTER_CRITICAL(&event_ring_lock);
    const CaptureDropStats stats = {
        .no_slot = dropped_no_slot,
        .ring_full = dropped_ring_full,
        .parse_failed = dropped_parse_failed,
    };
    dropped_no_slot = 0;
    dropped_ring_full = 0;
    dropped_parse_failed = 0;
    portEXIT_CRITICAL(&event_ring_lock);
    return stats;
}

} // namespace Internal
} // namespace BlackboxService
