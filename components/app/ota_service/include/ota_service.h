#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace OtaService {

enum class State : uint8_t {
    IDLE = 0,
    CHECKING,
    UPDATE_AVAILABLE,
    UP_TO_DATE,
    DOWNLOADING,
    VERIFYING,
    RESTARTING,
    FAILED,
};

struct Status {
    State state;
    char current_version[32];
    char latest_version[32];
    char active_source[24];
    char last_error[64];
    size_t bytes_downloaded;
    size_t image_size;
};

/** @brief 初始化远端 OTA 服务状态和同步资源。 */
esp_err_t init();

/** @brief 异步检查远端版本。忙碌时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t request_check();

/**
 * @brief 异步执行在线升级。
 *
 * 若尚未检查版本，会先检查；仅远端版本严格高于当前版本时下载。
 * 下载、校验和激活成功后自动重启。
 */
esp_err_t request_upgrade();

/** @brief 获取线程安全的远端 OTA 状态快照。 */
Status get_status();

/** @brief 将状态枚举转换为 API 使用的稳定字符串。 */
const char* state_to_string(State state);

} // namespace OtaService

#endif
