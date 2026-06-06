#ifndef ESPNOW_SERVICE_H
#define ESPNOW_SERVICE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "espnow_link.h"

namespace EspNowService {

/** 远程输出控制动作。 */
enum class SwitchAction : uint8_t {
    OFF = 0,
    ON = 1,
    TOGGLE = 2,
};

/** 远程输出控制的业务执行结果。 */
enum class SwitchResult : uint8_t {
    OK = 0,
    REJECTED = 1,
    NOT_READY = 2,
    INVALID_ACTION = 3,
    INTERNAL_ERROR = 4,
};

constexpr uint8_t DEVICE_STATUS_OUTPUT_ON = 1U << 0;

/**
 * @brief 产品实时数据快照
 *
 * 字段使用固定宽度整数和明确单位，线上协议由组件逐字段编码，不依赖结构体内存布局。
 * status_flags 的 bit0 表示输出开启，其余位由产品应用自行定义。
 */
struct DeviceData {
    uint16_t voltage_mv = 0;
    int32_t current_ua = 0;
    int16_t board_temperature_centi_c = 0;
    int16_t chip_temperature_centi_c = 0;
    int64_t charge_uah = 0;
    int64_t energy_uwh = 0;
    uint64_t meter_time_ms = 0;
    uint8_t status_flags = 0;
};

/**
 * @brief 收到控制请求时执行输出操作
 * @param source 请求设备 MAC
 * @param action 请求动作
 * @param output_on 返回执行后的实际输出状态
 * @param context 注册时提供的上下文
 * @note 回调在 espnow_business 任务中执行，不在 WiFi 或 espnow_link 回调中执行。
 */
using SwitchRequestHandler = SwitchResult (*)(const EspNowLink::MacAddress& source,
                                              SwitchAction action,
                                              bool* output_on,
                                              void* context);

/** 收到控制响应时通知请求方。 */
using SwitchResponseHandler = void (*)(const EspNowLink::MacAddress& source,
                                       uint32_t request_id,
                                       SwitchAction action,
                                       SwitchResult result,
                                       bool output_on,
                                       void* context);

/**
 * @brief 收到数据读取请求时生成当前数据
 * @return true 已生成有效数据；false 当前不能提供数据
 */
using DataRequestHandler = bool (*)(const EspNowLink::MacAddress& source,
                                    DeviceData* data,
                                    void* context);

/**
 * @brief 收到数据响应或周期上报时通知应用
 * @param request_id 数据响应对应的请求 ID；周期上报固定为 0
 * @param available false 表示目标当前不能提供请求的数据
 * @param periodic true 表示尽力传输的周期上报，false 表示可靠请求响应
 */
using DataReceivedHandler = void (*)(const EspNowLink::MacAddress& source,
                                     uint32_t request_id,
                                     const DeviceData& data,
                                     bool available,
                                     bool periodic,
                                     void* context);

/**
 * @brief 初始化 ESP-NOW 产品服务并从 NVS 恢复已保存 peer
 *
 * 初始化配对、peer 管理和远程开关业务分发。初始化不自动进入配对模式，
 * 控制器和远程开关通过 role 选择配对职责。
 */
/** Initialize only the product business protocol and callback dispatcher. */
esp_err_t init();

/** @brief 注册或清除控制请求处理器，handler 为 nullptr 时清除。 */
void set_switch_request_handler(SwitchRequestHandler handler, void* context = nullptr);
/** @brief 注册或清除控制响应通知，handler 为 nullptr 时清除。 */
void set_switch_response_handler(SwitchResponseHandler handler, void* context = nullptr);
/** @brief 注册或清除数据提供器，handler 为 nullptr 时清除。 */
void set_data_request_handler(DataRequestHandler handler, void* context = nullptr);
/** @brief 注册或清除数据接收通知，handler 为 nullptr 时清除。 */
void set_data_received_handler(DataReceivedHandler handler, void* context = nullptr);

/**
 * @brief 可靠发送开关控制请求
 * @param request_id 返回本次业务请求 ID，可传 nullptr
 * @param callback 链路发送结果回调，可用于诊断 NO_ACK
 */
esp_err_t send_switch_request(const EspNowLink::MacAddress& destination,
                              SwitchAction action,
                              uint32_t* request_id = nullptr,
                              EspNowLink::SendCallback callback = nullptr,
                              void* context = nullptr);

/**
 * @brief 可靠请求目标设备返回实时数据
 * @param request_id 返回本次业务请求 ID，可传 nullptr
 */
esp_err_t request_device_data(const EspNowLink::MacAddress& destination,
                              uint32_t* request_id = nullptr,
                              EspNowLink::SendCallback callback = nullptr,
                              void* context = nullptr);

/**
 * @brief 尽力发送周期数据，不等待业务响应
 *
 * 单播使用已配对 peer 加密，广播由 espnow_link 自动改为明文尽力传输。
 */
esp_err_t send_periodic_data(const EspNowLink::MacAddress& destination,
                             const DeviceData& data,
                             EspNowLink::SendCallback callback = nullptr,
                             void* context = nullptr);

} // namespace EspNowService

#endif
