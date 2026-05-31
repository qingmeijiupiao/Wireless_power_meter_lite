/*
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子日志系统实现
 * @Author: qingmeijiupiao
 */
#include "blackbox.h"
#include "circular_flash_buffer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace Blackbox;

static bool enabled = true;

static uint8_t CRC8_Calc(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; ++j) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t write_record(Record& raw) {
    raw.crc_checksum = CRC8_Calc(reinterpret_cast<uint8_t*>(&raw), sizeof(Record) - 1);
    return CircularFlashBuffer::write_block(reinterpret_cast<uint8_t*>(&raw));
}

esp_err_t Blackbox::init() {
    enabled = true;
    return CircularFlashBuffer::init("blackbox", sizeof(Record));
}

esp_err_t Blackbox::append_text(const char *fmt, ...) {
    if (fmt == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    char buf[TEXT_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = strlen(buf);

    if (len < PAYLOAD_SIZE) {
        Record raw;
        raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
        raw.header.type = LogType::STRING;
        raw.header.timestamp = esp_timer_get_time() / 1000;
        memset(raw.payload.bytes, 0, PAYLOAD_SIZE);
        memcpy(raw.payload.str, buf, len + 1);
        return write_record(raw);
    }

    uint8_t fragments = (len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
    if (fragments > MAX_TEXT_FRAGMENTS) fragments = MAX_TEXT_FRAGMENTS;

    for (uint8_t i = 0; i < fragments; i++) {
        Record raw;
        raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
        raw.header.type = LogType::STRING;
        raw.header.timestamp = esp_timer_get_time() / 1000;
        memset(raw.payload.bytes, 0, PAYLOAD_SIZE);

        size_t offset = i * PAYLOAD_SIZE;
        if (i == fragments - 1) {
            size_t remaining = len - offset;
            if (remaining >= PAYLOAD_SIZE) remaining = PAYLOAD_SIZE - 1;
            memcpy(raw.payload.str, buf + offset, remaining);
            raw.payload.str[remaining] = '\0';
        } else {
            memcpy(raw.payload.str, buf + offset, PAYLOAD_SIZE);
        }

        esp_err_t err = write_record(raw);
        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

esp_err_t Blackbox::append_typed(LogType type, const uint8_t* payload, size_t len) {
    if (payload == nullptr || len > PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    Record raw;
    raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
    raw.header.type = type;
    raw.header.timestamp = esp_timer_get_time() / 1000;
    memset(raw.payload.bytes, 0, PAYLOAD_SIZE);
    memcpy(raw.payload.bytes, payload, len);

    return write_record(raw);
}

uint32_t Blackbox::count() {
    return CircularFlashBuffer::get_count();
}

Record Blackbox::read(uint32_t index) {
    Record raw = {};

    esp_err_t err = CircularFlashBuffer::read_block(index, reinterpret_cast<uint8_t*>(&raw));
    if (err != ESP_OK) {
        return raw;
    }

    uint8_t calc_crc = CRC8_Calc(reinterpret_cast<uint8_t*>(&raw), sizeof(Record) - 1);
    if (raw.header.sof != CircularFlashBuffer::BLOCK_SOF || raw.crc_checksum != calc_crc) {
        ESP_LOGW("BlackBox", "Log at index %d has invalid SOF or CRC8", index);
        raw = {};
    }

    return raw;
}

TextRecord Blackbox::read_text(uint32_t index) {
    TextRecord text = {};
    Record fragments[MAX_TEXT_FRAGMENTS];

    fragments[0] = read(index);
    if (fragments[0].header.sof != CircularFlashBuffer::BLOCK_SOF ||
        fragments[0].header.type != LogType::STRING ||
        memchr(fragments[0].payload.str, '\0', PAYLOAD_SIZE) == nullptr) {
        return text;
    }

    text.record_count = 1;
    uint32_t raw_count = count();
    // 更早的分片位于更旧的索引，且末尾没有 NUL 字符。
    while (text.record_count < MAX_TEXT_FRAGMENTS &&
           index + text.record_count < raw_count) {
        Record previous = read(index + text.record_count);
        if (previous.header.sof != CircularFlashBuffer::BLOCK_SOF ||
            previous.header.type != LogType::STRING ||
            memchr(previous.payload.str, '\0', PAYLOAD_SIZE) != nullptr) {
            break;
        }
        fragments[text.record_count++] = previous;
    }

    size_t offset = 0;
    // 按从旧到新的顺序复制分片，恢复原始字符串顺序。
    for (uint8_t i = text.record_count; i > 0; --i) {
        const Record& fragment = fragments[i - 1];
        size_t len = strnlen(fragment.payload.str, PAYLOAD_SIZE);
        memcpy(text.str + offset, fragment.payload.str, len);
        offset += len;
    }
    text.str[offset] = '\0';

    return text;
}

void Blackbox::set_enabled(bool enable) {
    if (enable) {
        CircularFlashBuffer::set_enable(true);
        enabled = true;
        append_text("[Blackbox]: enabled");
        return;
    }
    append_text("[Blackbox]: disabled");
    enabled = false;
    CircularFlashBuffer::set_enable(false);
}

bool Blackbox::is_enabled() {
    return enabled;
}
