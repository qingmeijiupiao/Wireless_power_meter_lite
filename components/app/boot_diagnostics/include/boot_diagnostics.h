/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 启动时的日志打印
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:38:23
 */
#ifndef BOOT_DIAGNOSTICS_H
#define BOOT_DIAGNOSTICS_H

#include "esp_err.h"

namespace BootDiagnostics {

/**
 * @brief 记录当前启动阶段。
 * @param stage 稳定的启动阶段名称。
 */
void append_stage(const char* stage);

/**
 * @brief 记录硬件配置检测失败事件。
 * @param err 硬件配置初始化返回的错误码。
 */
void append_hardware_config_failure(esp_err_t err);

/** @brief 记录一次系统启动事件。 */
void append_system_boot_start();

/** @brief 记录启动早期可获取的固件、硬件和配置诊断信息。 */
void append_early();

/** @brief 记录各业务组件启动后的运行期诊断摘要。 */
void append_runtime();

} // namespace BootDiagnostics

#endif
