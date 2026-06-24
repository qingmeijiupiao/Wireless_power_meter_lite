/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: shell命令管理
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:40:07
 */
#ifndef SHELL_COMMAND_H
#define SHELL_COMMAND_H

#include "esp_err.h"
#include "shell.h"
namespace ShellCommand {

/**
 * @brief 初始化 Shell 并注册产品命令。
 * @return ESP_OK 初始化成功；其他值表示 Shell 或命令注册失败。

 */
esp_err_t init();

} // namespace ShellCommand

#endif
