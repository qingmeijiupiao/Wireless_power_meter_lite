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

namespace BlackBoxStructured {
    struct StructuredPayload_t {
        GlobalState global_state;
    } __attribute__((packed));

    static_assert(sizeof(StructuredPayload_t) <= BlackBox::PAYLOAD_SIZE,
                  "StructuredPayload_t exceeds payload size");

    esp_err_t add_structured_log();
}

#endif
