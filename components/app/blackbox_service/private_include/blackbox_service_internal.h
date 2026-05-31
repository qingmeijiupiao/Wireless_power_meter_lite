/*
 * @Description: blackbox_service 组件内部接口
 */
#ifndef BLACKBOX_SERVICE_INTERNAL_H
#define BLACKBOX_SERVICE_INTERNAL_H

#include <stdint.h>

#include "blackbox.h"
#include "esp_err.h"

namespace BlackboxService {
namespace Internal {

struct LogEvent {
    bool include_text;
    char text[Blackbox::TEXT_BUFFER_SIZE];
};

/** @brief 从 NVS 恢复周期快照配置。 */
void load_config();

/** @brief 线程安全地读取运行期周期快照配置。 */
uint32_t read_snapshot_interval_s();

/** @brief 更新运行期配置并持久化到 NVS。 */
void write_snapshot_interval_s(uint32_t seconds);

/** @brief 安装 ESP_LOG vprintf 钩子。 */
void install_log_capture();

/** @brief 从日志捕获 RAM 环中弹出一条事件。 */
bool pop_log_event(LogEvent* event);

/** @brief 启动事件消费与周期快照后台任务。 */
esp_err_t start_worker();

} // namespace Internal
} // namespace BlackboxService

#endif
