/*
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子结构化数据日志，定义业务相关的 payload 格式并写入
 * @Author: qingmeijiupiao
 */
#ifndef BLACKBOX_STRUCTURED_H
#define BLACKBOX_STRUCTURED_H
#include "blackbox.h"
#include "global_state.h"
#include "esp_err.h"

namespace BlackboxStructured {
    constexpr uint8_t SNAPSHOT_VERSION = 1;

    struct SnapshotV1 {
        uint8_t version;
        GlobalStateFlags flags;
        protect_states_t protect_states;
        uint16_t voltage_mV;
        int32_t current_uA;
        int32_t meter_mwh;
        int16_t board_temperature;
        int16_t chip_temperature;
    } __attribute__((packed));

    static_assert(sizeof(SnapshotV1) == 20, "SnapshotV1 size mismatch");
    static_assert(sizeof(SnapshotV1) <= Blackbox::PAYLOAD_SIZE,
                  "SnapshotV1 exceeds blackbox payload size");

    esp_err_t append_snapshot();
}

#endif
