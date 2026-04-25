/*
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子日志系统，支持字符串日志与类型化数据日志，与具体业务数据结构解耦
 * @Author: qingmeijiupiao
 */
#ifndef BLACKBOX_H
#define BLACKBOX_H
#include <stdint.h>
#include "esp_err.h"
#include "circular_flash_buffer.h"

namespace BlackBox {
    constexpr uint32_t BLACKBOX_DATA_SIZE = 32;

    enum class LogType : uint8_t {
        STRING = 0,
        STRUCTURED = 1,
    };

    struct BlackBoxHeader_t {
        uint8_t sof = CircularFlashBuffer::BLOCK_SOF;
        LogType type;
        uint32_t timestamp;
    } __attribute__((packed));

    constexpr uint8_t PAYLOAD_SIZE = BLACKBOX_DATA_SIZE - sizeof(BlackBoxHeader_t) - 1;
    static_assert(PAYLOAD_SIZE == BLACKBOX_DATA_SIZE - sizeof(BlackBoxHeader_t) - sizeof(uint8_t), "PAYLOAD_SIZE mismatch");

    union BlackBoxPayload_t {
        uint8_t bytes[PAYLOAD_SIZE];
        char str[PAYLOAD_SIZE];
    } __attribute__((packed));

    struct BlackBoxRaw_t {
        BlackBoxHeader_t header;
        BlackBoxPayload_t payload;
        uint8_t crc_checksum;
    } __attribute__((packed));
    static_assert(sizeof(BlackBoxRaw_t) == BLACKBOX_DATA_SIZE, "BlackBoxRaw_t size mismatch");

    esp_err_t init();

    esp_err_t add_string_log(const char *fmt, ...);

    esp_err_t add_typed_log(LogType type, const uint8_t* payload, size_t len);

    uint32_t get_count();

    BlackBoxRaw_t get_log(uint32_t index);

    void set_log_enable(bool enable);
}

#endif
