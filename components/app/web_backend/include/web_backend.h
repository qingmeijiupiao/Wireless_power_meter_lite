/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: Web 后端管理组件，集中注册设备控制页、配网页和业务 API
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-28
 */
#ifndef WEB_BACKEND_H
#define WEB_BACKEND_H

#include "esp_err.h"

namespace WebBackend {

/** @brief 初始化后端路由与中间件 */
esp_err_t init();

/** @brief 启动后端 HTTP 服务 */
esp_err_t start();

/**
 * @brief 按 NVS 配置启动 WiFi/Web 后端
 *
 * 内部会初始化 WebBackend。若 WifiService 配置为开机启用，则启动 WifiService 默认模式并启动 HTTP 服务。
 * WiFi 或 Web 启动失败时只记录日志并返回错误码，不触发设备重启。
 */
esp_err_t start_with_wifi_service();

/** @brief 停止后端 HTTP 服务 */
esp_err_t stop();

/** @brief 查询后端 HTTP 服务是否运行 */
bool is_running();

} // namespace WebBackend

#endif
