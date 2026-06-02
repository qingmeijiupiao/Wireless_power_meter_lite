/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description:  LP 核加载模块，负责加载 LP 核二进制文件并启动 LP 核
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-04-05 18:12:38
 */

#ifndef ULP_LOADER_HPP
#define ULP_LOADER_HPP

#include "esp_err.h"
#include "ulp_main.h"
#include "ulp_app/ulp_state.h"
#include <stdint.h>

esp_err_t LP_Core_Load(void);

struct LP_Core_Snapshot {
    ULP_CORE_STATE state;
    uint32_t log_data;
    uint32_t voltage_uv;
    int32_t current_uA;
    int16_t shunt_register_raw;
    uint16_t voltage_register_raw;
    uint16_t ina226_manufacturer_id;
    int64_t meter_uah;
    int64_t meter_uwh;
};

bool LP_Core_GetSnapshot(LP_Core_Snapshot* snapshot);
void LP_Core_SetBoardTemperature(int32_t temperature);

#endif // ULP_LOADER_HPP
