/*
 * @LastEditors: qingmeijiupiao
 * @Description: 黑匣子结构化数据日志实现
 * @Author: qingmeijiupiao
 */
#include "blackbox_structured.h"

using namespace BlackBoxStructured;

static auto& global_state_ref = get_global_state();

esp_err_t BlackBoxStructured::add_structured_log() {
    StructuredPayload_t payload;
    payload.global_state = global_state_ref;
    return BlackBox::add_typed_log(BlackBox::LogType::STRUCTURED,
                                   reinterpret_cast<uint8_t*>(&payload),
                                   sizeof(StructuredPayload_t));
}
