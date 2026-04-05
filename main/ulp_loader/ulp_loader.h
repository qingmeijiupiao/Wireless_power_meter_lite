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

esp_err_t LP_Core_Load(void);

#endif // ULP_LOADER_HPP