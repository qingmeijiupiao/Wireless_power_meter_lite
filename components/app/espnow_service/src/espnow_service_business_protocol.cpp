#include "espnow_service_internal.h"

namespace EspNowService::Internal {
namespace {

constexpr size_t SWITCH_REQUEST_SIZE = 5;
constexpr size_t SWITCH_RESPONSE_SIZE = 7;
constexpr size_t DATA_REQUEST_SIZE = 4;
constexpr size_t DATA_MESSAGE_SIZE = 40;

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_u32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t read_u64(const uint8_t* data) {
    return static_cast<uint64_t>(read_u32(data)) |
           (static_cast<uint64_t>(read_u32(data + 4)) << 32);
}

void write_u16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
}

void write_u32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
}

void write_u64(uint8_t* data, uint64_t value) {
    write_u32(data, static_cast<uint32_t>(value));
    write_u32(data + 4, static_cast<uint32_t>(value >> 32));
}

bool valid_action(uint8_t value) {
    return value <= static_cast<uint8_t>(SwitchAction::TOGGLE);
}

bool valid_result(uint8_t value) {
    return value <= static_cast<uint8_t>(SwitchResult::INTERNAL_ERROR);
}

} // namespace

size_t encode_switch_request(const SwitchRequest& request, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < SWITCH_REQUEST_SIZE) {
        return 0;
    }
    write_u32(output, request.request_id);
    output[4] = static_cast<uint8_t>(request.action);
    return SWITCH_REQUEST_SIZE;
}

bool decode_switch_request(const EspNowLink::Message& message, SwitchRequest* request) {
    if (request == nullptr || message.payload_size != SWITCH_REQUEST_SIZE ||
        !valid_action(message.payload[4])) {
        return false;
    }
    request->request_id = read_u32(message.payload);
    request->action = static_cast<SwitchAction>(message.payload[4]);
    return request->request_id != 0;
}

size_t encode_switch_response(const SwitchResponse& response,
                              uint8_t* output,
                              size_t capacity) {
    if (output == nullptr || capacity < SWITCH_RESPONSE_SIZE) {
        return 0;
    }
    write_u32(output, response.request_id);
    output[4] = static_cast<uint8_t>(response.action);
    output[5] = static_cast<uint8_t>(response.result);
    output[6] = response.output_on ? 1 : 0;
    return SWITCH_RESPONSE_SIZE;
}

bool decode_switch_response(const EspNowLink::Message& message, SwitchResponse* response) {
    if (response == nullptr || message.payload_size != SWITCH_RESPONSE_SIZE ||
        !valid_action(message.payload[4]) || !valid_result(message.payload[5]) ||
        message.payload[6] > 1) {
        return false;
    }
    response->request_id = read_u32(message.payload);
    response->action = static_cast<SwitchAction>(message.payload[4]);
    response->result = static_cast<SwitchResult>(message.payload[5]);
    response->output_on = message.payload[6] != 0;
    return response->request_id != 0;
}

size_t encode_data_request(uint32_t request_id, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < DATA_REQUEST_SIZE) {
        return 0;
    }
    write_u32(output, request_id);
    return DATA_REQUEST_SIZE;
}

bool decode_data_request(const EspNowLink::Message& message, uint32_t* request_id) {
    if (request_id == nullptr || message.payload_size != DATA_REQUEST_SIZE) {
        return false;
    }
    *request_id = read_u32(message.payload);
    return *request_id != 0;
}

size_t encode_data_message(const DataMessage& message, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < DATA_MESSAGE_SIZE) {
        return 0;
    }
    write_u32(output, message.request_id);
    output[4] = message.available ? 1 : 0;
    output[5] = message.data.status_flags;
    write_u16(output + 6, message.data.voltage_mv);
    write_u32(output + 8, static_cast<uint32_t>(message.data.current_ua));
    write_u16(output + 12, static_cast<uint16_t>(message.data.board_temperature_centi_c));
    write_u16(output + 14, static_cast<uint16_t>(message.data.chip_temperature_centi_c));
    write_u64(output + 16, static_cast<uint64_t>(message.data.charge_uah));
    write_u64(output + 24, static_cast<uint64_t>(message.data.energy_uwh));
    write_u64(output + 32, message.data.meter_time_ms);
    return DATA_MESSAGE_SIZE;
}

bool decode_data_message(const EspNowLink::Message& message, DataMessage* data) {
    if (data == nullptr || message.payload_size != DATA_MESSAGE_SIZE ||
        message.payload[4] > 1) {
        return false;
    }
    data->request_id = read_u32(message.payload);
    data->available = message.payload[4] != 0;
    data->data.status_flags = message.payload[5];
    data->data.voltage_mv = read_u16(message.payload + 6);
    data->data.current_ua = static_cast<int32_t>(read_u32(message.payload + 8));
    data->data.board_temperature_centi_c =
        static_cast<int16_t>(read_u16(message.payload + 12));
    data->data.chip_temperature_centi_c =
        static_cast<int16_t>(read_u16(message.payload + 14));
    data->data.charge_uah = static_cast<int64_t>(read_u64(message.payload + 16));
    data->data.energy_uwh = static_cast<int64_t>(read_u64(message.payload + 24));
    data->data.meter_time_ms = read_u64(message.payload + 32);
    return true;
}

} // namespace EspNowService::Internal
