/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子日志系统实现
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-05-31 19:56:12
 */

#include "blackbox.h"
#include "circular_flash_buffer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace Blackbox;

// 运行状态标志
static bool enabled = true;
// FreeRTOS 消息队列句柄，用于存储等待写入 Flash 的日志记录
static QueueHandle_t log_queue = nullptr;
// 异步写入任务句柄
static TaskHandle_t blackbox_task_handle = nullptr;
// 队列深度：可缓冲的记录条数。设为 64 可应对短时间的日志爆发
static constexpr uint32_t QUEUE_SIZE = 64;

/**
 * @brief 计算 CRC8 校验值，用于检测 Flash 数据损坏
 * @param data 数据指针
 * @param length 数据长度
 * @return uint8_t 校验和
 */
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

/**
 * @brief 内部物理写入函数。
 * @note 此函数由独立的异步任务调用，包含校验计算和物理 Flash 操作。
 */
static esp_err_t write_record_internal(Record& raw) {
    // 写入前计算 CRC，覆盖除校验位自身外的所有字段
    raw.crc_checksum = CRC8_Calc(reinterpret_cast<uint8_t*>(&raw), sizeof(Record) - 1);
    return CircularFlashBuffer::write_block(reinterpret_cast<uint8_t*>(&raw));
}

/**
 * @brief 黑匣子异步写入任务（消费者）
 * @details 持续监听队列，一旦有新日志则执行 Flash 写入操作。
 */
static void blackbox_task(void* arg) {
    Record record;
    ESP_LOGI("Blackbox", "Async task started");
    while (true) {
        // 无限期等待队列消息
        if (xQueueReceive(log_queue, &record, portMAX_DELAY) == pdPASS) {
            if (enabled) {
                write_record_internal(record);
            }
        }
    }
}

/**
 * @brief 将记录推入异步队列（生产者接口）
 * @param raw 构造好的日志记录
 * @return esp_err_t ESP_OK 表示成功，ESP_FAIL 表示队列满导致丢弃
 */
static esp_err_t queue_record(Record& raw) {
    if (log_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 超时时间设为 0：如果队列满了，立即返回失败并丢弃日志。
    // 这样做是为了绝对保证调用者（如过流保护任务）不会因为日志拥堵而产生延迟。
    if (xQueueSend(log_queue, &raw, 0) != pdPASS) {
        return ESP_FAIL; 
    }
    return ESP_OK;
}

/**
 * @brief 初始化黑匣子系统
 * @details 初始化底层循环缓冲区、创建消息队列并启动异步写入任务。
 */
esp_err_t Blackbox::init() {
    if (log_queue != nullptr) {
        return ESP_OK;
    }

    // 初始化底层驱动（分配分区，设置块大小）
    esp_err_t err = CircularFlashBuffer::init("blackbox", sizeof(Record));
    if (err != ESP_OK) return err;

    // 创建 FreeRTOS 队列
    log_queue = xQueueCreate(QUEUE_SIZE, sizeof(Record));
    if (log_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // 创建异步日志任务，优先级设为 3（适中），堆栈 3KB
    BaseType_t ret = xTaskCreate(blackbox_task, "blackbox_t", 3072, nullptr, 3, &blackbox_task_handle);
    if (ret != pdPASS) {
        vQueueDelete(log_queue);
        log_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    enabled = true;
    return ESP_OK;
}

/**
 * @brief 写入格式化字符串日志
 * @param fmt printf 风格的格式化字符串
 * @details 支持长字符串自动切片存储。
 */
esp_err_t Blackbox::append_text(const char *fmt, ...) {
    if (fmt == nullptr || !enabled) {
        return ESP_ERR_INVALID_ARG;
    }

    char buf[TEXT_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = strlen(buf);

    // 情况 A：字符串较短，单条 Record 即可容纳
    if (len < PAYLOAD_SIZE) {
        Record raw;
        raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
        raw.header.type = LogType::STRING;
        raw.header.timestamp = esp_timer_get_time() / 1000;
        memset(raw.payload.bytes, 0, PAYLOAD_SIZE);
        memcpy(raw.payload.str, buf, len + 1);
        return queue_record(raw);
    }

    // 情况 B：长字符串，需要切分为多个碎片
    uint8_t fragments = (len + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
    if (fragments > MAX_TEXT_FRAGMENTS) fragments = MAX_TEXT_FRAGMENTS;

    for (uint8_t i = 0; i < fragments; i++) {
        Record raw;
        raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
        raw.header.type = LogType::STRING;
        raw.header.timestamp = esp_timer_get_time() / 1000;
        memset(raw.payload.bytes, 0, PAYLOAD_SIZE);

        size_t offset = i * PAYLOAD_SIZE;
        if (i == fragments - 1) { // 最后一个碎片
            size_t remaining = len - offset;
            if (remaining >= PAYLOAD_SIZE) remaining = PAYLOAD_SIZE - 1;
            memcpy(raw.payload.str, buf + offset, remaining);
            raw.payload.str[remaining] = '\0'; // 确保结尾有 NUL
        } else {
            memcpy(raw.payload.str, buf + offset, PAYLOAD_SIZE);
            // 中间碎片不带 NUL，作为后续拼接读取的判断依据
        }

        esp_err_t err = queue_record(raw);
        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

/**
 * @brief 写入类型化（结构化）原始数据日志
 * @param type 日志类型枚举
 * @param payload 数据指针
 * @param len 数据长度
 */
esp_err_t Blackbox::append_typed(LogType type, const uint8_t* payload, size_t len) {
    if (payload == nullptr || len > PAYLOAD_SIZE || !enabled) {
        return ESP_ERR_INVALID_ARG;
    }

    Record raw;
    raw.header.sof = CircularFlashBuffer::BLOCK_SOF;
    raw.header.type = type;
    raw.header.timestamp = esp_timer_get_time() / 1000;
    memset(raw.payload.bytes, 0, PAYLOAD_SIZE);
    memcpy(raw.payload.bytes, payload, len);

    return queue_record(raw);
}

/**
 * @brief 获取当前存储的总日志条数（不计已被擦除的旧日志）
 */
uint32_t Blackbox::count() {
    return CircularFlashBuffer::get_count();
}

/**
 * @brief 从 Flash 中读取单条原始记录并进行校验
 * @param index 索引（0 为最新记录）
 */
Record Blackbox::read(uint32_t index) {
    Record raw = {};

    esp_err_t err = CircularFlashBuffer::read_block(index, reinterpret_cast<uint8_t*>(&raw));
    if (err != ESP_OK) {
        return raw;
    }

    // 校验 SOF 标志和 CRC8
    uint8_t calc_crc = CRC8_Calc(reinterpret_cast<uint8_t*>(&raw), sizeof(Record) - 1);
    if (raw.header.sof != CircularFlashBuffer::BLOCK_SOF || raw.crc_checksum != calc_crc) {
        ESP_LOGW("BlackBox", "Log at index %d has invalid SOF or CRC8", index);
        raw = {};
    }

    return raw;
}

esp_err_t Blackbox::erase_all() {
    if (log_queue == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // 1. 重置异步队列，丢弃所有尚未写入 Flash 的日志
    xQueueReset(log_queue);

    // 2. 调用底层物理擦除
    esp_err_t err = CircularFlashBuffer::erase_all();
    if (err == ESP_OK) {
        append_text("[Blackbox]: Factory Reset Performed");
    }
    return err;
}

/**
 * @brief 读取并拼接完整的文本日志（处理碎片合并）
 * @param index 日志记录索引（应指向长字符串的末尾碎片）
 * @return TextRecord 包含拼接后的字符串和所占用的原始记录条数
 */
TextRecord Blackbox::read_text(uint32_t index) {
    TextRecord text = {};
    Record fragments[MAX_TEXT_FRAGMENTS];

    // 读取第一条记录（末尾碎片，必然包含 NUL）
    fragments[0] = read(index);
    if (fragments[0].header.sof != CircularFlashBuffer::BLOCK_SOF ||
        fragments[0].header.type != LogType::STRING ||
        memchr(fragments[0].payload.str, '\0', PAYLOAD_SIZE) == nullptr) {
        return text;
    }

    text.record_count = 1;
    uint32_t raw_count = count();
    // 向前搜索更早的碎片（这些碎片不包含 NUL）
    while (text.record_count < MAX_TEXT_FRAGMENTS &&
           index + text.record_count < raw_count) {
        Record previous = read(index + text.record_count);
        if (previous.header.sof != CircularFlashBuffer::BLOCK_SOF ||
            previous.header.type != LogType::STRING ||
            memchr(previous.payload.str, '\0', PAYLOAD_SIZE) != nullptr) {
            break; // 遇到非字符串或带 NUL 的记录，说明碎片结束
        }
        fragments[text.record_count++] = previous;
    }

    size_t offset = 0;
    // 按时间顺序（从旧到新）拼接字符串碎片
    for (uint8_t i = text.record_count; i > 0; --i) {
        const Record& fragment = fragments[i - 1];
        size_t len = strnlen(fragment.payload.str, PAYLOAD_SIZE);
        memcpy(text.str + offset, fragment.payload.str, len);
        offset += len;
    }
    text.str[offset] = '\0';

    return text;
}

/**
 * @brief 动态启用或禁用黑匣子功能
 */
void Blackbox::set_enabled(bool enable) {
    if (enable) {
        enabled = true;
        append_text("[Blackbox]: enabled");
        return;
    }
    append_text("[Blackbox]: disabled");
    enabled = false;
}

/**
 * @brief 查询黑匣子当前是否启用
 */
bool Blackbox::is_enabled() {
    return enabled;
}
