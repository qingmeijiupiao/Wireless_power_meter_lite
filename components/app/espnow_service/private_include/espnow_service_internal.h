#ifndef ESPNOW_SERVICE_INTERNAL_H
#define ESPNOW_SERVICE_INTERNAL_H

#include "espnow_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace EspNowService::Internal {

constexpr uint16_t MSG_SWITCH_REQUEST = 0x0200;
constexpr uint16_t MSG_SWITCH_RESPONSE = 0x0201;
constexpr uint16_t MSG_DATA_REQUEST = 0x0210;
constexpr uint16_t MSG_DATA_RESPONSE = 0x0211;
constexpr uint16_t MSG_DATA_PERIODIC = 0x0212;

struct SwitchRequest {
    uint32_t request_id;
    SwitchAction action;
};

struct SwitchResponse {
    uint32_t request_id;
    SwitchAction action;
    SwitchResult result;
    bool output_on;
};

struct DataMessage {
    uint32_t request_id;
    bool available;
    DeviceData data;
};

esp_err_t start_business_service();

size_t encode_switch_request(const SwitchRequest& request, uint8_t* output, size_t capacity);
bool decode_switch_request(const EspNowLink::Message& message, SwitchRequest* request);
size_t encode_switch_response(const SwitchResponse& response, uint8_t* output, size_t capacity);
bool decode_switch_response(const EspNowLink::Message& message, SwitchResponse* response);
size_t encode_data_request(uint32_t request_id, uint8_t* output, size_t capacity);
bool decode_data_request(const EspNowLink::Message& message, uint32_t* request_id);
size_t encode_data_message(const DataMessage& data, uint8_t* output, size_t capacity);
bool decode_data_message(const EspNowLink::Message& message, DataMessage* data);

void register_product_handlers();

} // namespace EspNowService::Internal

#endif
