/*
 * @LastEditors: qingmeijiupiao
 * @Description: 应用层黑匣子服务，管理状态快照、关键事件和周期记录
 * @Author: qingmeijiupiao
 */
#ifndef BLACKBOX_SERVICE_H
#define BLACKBOX_SERVICE_H

#include <stdint.h>

#include "blackbox.h"
#include "esp_err.h"
#include "global_state.h"

namespace BlackboxService {

constexpr uint8_t SNAPSHOT_VERSION = 1;
constexpr uint32_t DEFAULT_SNAPSHOT_INTERVAL_S = 0;
constexpr uint32_t MIN_SNAPSHOT_INTERVAL_MS = 100;

struct SnapshotV1 {
    uint8_t version;
    GlobalStateFlags flags;
    protect_states_t protect_states;
    uint16_t voltage_mV;
    int32_t current_uA;
    float meter_mwh;
    int16_t board_temperature;
    int16_t chip_temperature;
} __attribute__((packed));

static_assert(sizeof(SnapshotV1) == 20, "SnapshotV1 size mismatch");
static_assert(sizeof(SnapshotV1) <= Blackbox::PAYLOAD_SIZE,
              "SnapshotV1 exceeds blackbox payload size");

/** @brief 初始化 ESP_LOG 捕获和周期快照后台任务。 */
esp_err_t init();

/**
 * @brief 将当前全局状态写入一条结构化快照。
 *
 * 默认限制相邻快照至少间隔 MIN_SNAPSHOT_INTERVAL_MS。force=true 用于关键
 * 业务事件，忽略该限制并立即尝试入队。
 */
esp_err_t append_snapshot(bool force = false);

/**
 * @brief 写入关键状态变化或故障文本，并尝试追加一条全局状态快照。
 *
 * 文本优先写入。即使文本写入失败，也会继续尝试写入快照。
 */
esp_err_t append_event(const char* fmt, ...);

/**
 * @brief 仅写入文本事件，不追加状态快照。
 *
 * 用于配置、审计、操作记录和启动诊断块等低频信息，避免写入无必要快照。
 */
esp_err_t append_text_event(const char* fmt, ...);

/** @brief 获取周期快照间隔，0 表示关闭。 */
uint32_t get_snapshot_interval_s();

/** @brief 设置并持久化周期快照间隔，0 表示关闭。 */
void set_snapshot_interval_s(uint32_t seconds, const char* source);

} // namespace BlackboxService

#endif
