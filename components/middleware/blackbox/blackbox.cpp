/*
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子日志系统实现
 * @Author: qingmeijiupiao
 */
#include "blackbox.h"
#include "circular_flash_buffer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

using namespace BlackBox;

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

static esp_err_t write_raw(BlackBoxRaw_t& raw) {
    raw.crc_checksum = CRC8_Calc(reinterpret_cast<uint8_t*>(&raw), sizeof(BlackBoxRaw_t) - 1);
    return CircularFlashBuffer::write_block(reinterpret_cast<uint8_t*>(&raw));
}

esp_err_t BlackBox::init() {
    return CircularFlashBuffer::init("blackbox", sizeof(BlackBoxRaw_t));
}

esp_err_t BlackBox::add_string_log(const char *fmt, ...) {
    constexpr uint8_t MAX_FRAGMENTS = 3;
    char buf[MAX_FRAGMENTS * PAYLOAD_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = strlen(buf);

    if (len < PAYLOAD_SIZE) {
        BlackBoxRaw_t raw;
        raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
        raw.header.type = LogType::STRING;
        raw.header.timestamp = esp_timer_get_time() / 1000;
        memset(raw.payload.bytes, 0, PAYLOAD_SIZE);
        memcpy(raw.payload.str, buf, len + 1);
        return write_raw(raw);
    }

    uint8_t fragments = (len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
    if (fragments > MAX_FRAGMENTS) fragments = MAX_FRAGMENTS;

    for (uint8_t i = 0; i < fragments; i++) {
        BlackBoxRaw_t raw;
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

        esp_err_t err = write_raw(raw);
        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

esp_err_t BlackBox::add_typed_log(LogType type, const uint8_t* payload, size_t len) {
    if (len > PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    BlackBoxRaw_t raw;
    raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
    raw.header.type = type;
    raw.header.timestamp = esp_timer_get_time() / 1000;
    memset(raw.payload.bytes, 0, PAYLOAD_SIZE);
    memcpy(raw.payload.bytes, payload, len);

    return write_raw(raw);
}

uint32_t BlackBox::get_count() {
    return CircularFlashBuffer::get_count();
}

BlackBoxRaw_t BlackBox::get_log(uint32_t index) {
    BlackBoxRaw_t raw = {};

    esp_err_t err = CircularFlashBuffer::read_block(index, reinterpret_cast<uint8_t*>(&raw));
    if (err != ESP_OK) {
        return raw;
    }

    uint8_t calc_crc = CRC8_Calc(reinterpret_cast<uint8_t*>(&raw), sizeof(BlackBoxRaw_t) - 1);
    if (raw.header.sof != CircularFlashBuffer::BLOCK_SOF || raw.crc_checksum != calc_crc) {
        ESP_LOGW("BlackBox", "Log at index %d has invalid SOF or CRC8", index);
        raw = {};
    }

    return raw;
}

void BlackBox::set_log_enable(bool enable) {
    add_string_log("[BlackBox] : %s", enable ? "enabled" : "disabled");
    CircularFlashBuffer::set_enable(enable);
}
