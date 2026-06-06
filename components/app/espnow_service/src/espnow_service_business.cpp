#include "espnow_service.h"

#include "esp_log.h"
#include "esp_random.h"
#include "espnow_service_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace EspNowService {
namespace Internal {
namespace {

constexpr char TAG[] = "EspNowBusiness";
constexpr size_t BUSINESS_QUEUE_LENGTH = 12;
constexpr uint32_t BUSINESS_TASK_STACK_SIZE = 4096;
constexpr UBaseType_t BUSINESS_TASK_PRIORITY = 3;

enum class BusinessEventType : uint8_t {
    SWITCH_REQUEST,
    SWITCH_RESPONSE,
    DATA_REQUEST,
    DATA_RESPONSE,
    DATA_PERIODIC,
};

struct BusinessEvent {
    BusinessEventType type;
    EspNowLink::MacAddress source;
    SwitchRequest switch_request;
    SwitchResponse switch_response;
    DataMessage data_message;
};

template<typename Handler>
struct CallbackSlot {
    Handler handler;
    void* context;
};

QueueHandle_t business_queue = nullptr;
TaskHandle_t business_task_handle = nullptr;
portMUX_TYPE callback_lock = portMUX_INITIALIZER_UNLOCKED;
CallbackSlot<SwitchRequestHandler> switch_request_slot = {};
CallbackSlot<SwitchResponseHandler> switch_response_slot = {};
CallbackSlot<DataRequestHandler> data_request_slot = {};
CallbackSlot<DataReceivedHandler> data_received_slot = {};

uint32_t next_request_id() {
    uint32_t value = esp_random();
    return value == 0 ? 1 : value;
}

template<typename Handler>
CallbackSlot<Handler> callback_snapshot(const CallbackSlot<Handler>& slot) {
    portENTER_CRITICAL(&callback_lock);
    const CallbackSlot<Handler> result = slot;
    portEXIT_CRITICAL(&callback_lock);
    return result;
}

template<typename Handler>
void set_callback(CallbackSlot<Handler>* slot, Handler handler, void* context) {
    portENTER_CRITICAL(&callback_lock);
    slot->handler = handler;
    slot->context = handler == nullptr ? nullptr : context;
    portEXIT_CRITICAL(&callback_lock);
}

void business_link_handler(const EspNowLink::Message& message, void*) {
    BusinessEvent event = {};
    event.source = message.source;

    switch (message.message_id) {
        case MSG_SWITCH_REQUEST:
            if (!message.reliable || message.destination.is_broadcast()) {
                return;
            }
            event.type = BusinessEventType::SWITCH_REQUEST;
            if (!decode_switch_request(message, &event.switch_request)) {
                return;
            }
            break;
        case MSG_SWITCH_RESPONSE:
            if (!message.reliable || message.destination.is_broadcast()) {
                return;
            }
            event.type = BusinessEventType::SWITCH_RESPONSE;
            if (!decode_switch_response(message, &event.switch_response)) {
                return;
            }
            break;
        case MSG_DATA_REQUEST:
            if (!message.reliable || message.destination.is_broadcast()) {
                return;
            }
            event.type = BusinessEventType::DATA_REQUEST;
            if (!decode_data_request(message, &event.data_message.request_id)) {
                return;
            }
            break;
        case MSG_DATA_RESPONSE:
            if (!message.reliable || message.destination.is_broadcast()) {
                return;
            }
            event.type = BusinessEventType::DATA_RESPONSE;
            if (!decode_data_message(message, &event.data_message) ||
                event.data_message.request_id == 0) {
                return;
            }
            break;
        case MSG_DATA_PERIODIC:
            if (message.reliable) {
                return;
            }
            event.type = BusinessEventType::DATA_PERIODIC;
            if (!decode_data_message(message, &event.data_message) ||
                event.data_message.request_id != 0 || !event.data_message.available) {
                return;
            }
            break;
        default:
            return;
    }

    // Link 任务只做固定长度解码和入队，用户业务代码由独立任务调用。
    if (business_queue == nullptr ||
        xQueueSend(business_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "business event queue full");
    }
}

EspNowLink::SendOptions reliable_options() {
    EspNowLink::SendOptions options = {};
    options.delivery = EspNowLink::Delivery::RELIABLE;
    return options;
}

void handle_switch_request(const BusinessEvent& event) {
    SwitchResponse response = {};
    response.request_id = event.switch_request.request_id;
    response.action = event.switch_request.action;
    response.result = SwitchResult::NOT_READY;

    const auto slot = callback_snapshot(switch_request_slot);
    if (slot.handler != nullptr) {
        response.result = slot.handler(event.source,
                                       event.switch_request.action,
                                       &response.output_on,
                                       slot.context);
    }

    uint8_t payload[7] = {};
    const size_t size = encode_switch_response(response, payload, sizeof(payload));
    EspNowLink::send(event.source,
                     MSG_SWITCH_RESPONSE,
                     payload,
                     size,
                     reliable_options());
}

void handle_switch_response(const BusinessEvent& event) {
    const auto slot = callback_snapshot(switch_response_slot);
    if (slot.handler == nullptr) {
        return;
    }
    slot.handler(event.source,
                 event.switch_response.request_id,
                 event.switch_response.action,
                 event.switch_response.result,
                 event.switch_response.output_on,
                 slot.context);
}

void handle_data_request(const BusinessEvent& event) {
    DataMessage response = {};
    response.request_id = event.data_message.request_id;

    const auto slot = callback_snapshot(data_request_slot);
    if (slot.handler != nullptr) {
        response.available = slot.handler(event.source, &response.data, slot.context);
    }

    uint8_t payload[40] = {};
    const size_t size = encode_data_message(response, payload, sizeof(payload));
    EspNowLink::send(event.source,
                     MSG_DATA_RESPONSE,
                     payload,
                     size,
                     reliable_options());
}

void handle_data_received(const BusinessEvent& event, bool periodic) {
    const auto slot = callback_snapshot(data_received_slot);
    if (slot.handler == nullptr) {
        return;
    }
    slot.handler(event.source,
                 event.data_message.request_id,
                 event.data_message.data,
                 event.data_message.available,
                 periodic,
                 slot.context);
}

void business_task(void*) {
    BusinessEvent event = {};
    while (true) {
        if (xQueueReceive(business_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        // 所有产品业务回调在此任务串行执行，避免阻塞 WiFi 和 Link 任务。
        switch (event.type) {
            case BusinessEventType::SWITCH_REQUEST:
                handle_switch_request(event);
                break;
            case BusinessEventType::SWITCH_RESPONSE:
                handle_switch_response(event);
                break;
            case BusinessEventType::DATA_REQUEST:
                handle_data_request(event);
                break;
            case BusinessEventType::DATA_RESPONSE:
                handle_data_received(event, false);
                break;
            case BusinessEventType::DATA_PERIODIC:
                handle_data_received(event, true);
                break;
        }
    }
}

esp_err_t register_business_handlers() {
    const uint16_t ids[] = {
        MSG_SWITCH_REQUEST,
        MSG_SWITCH_RESPONSE,
        MSG_DATA_REQUEST,
        MSG_DATA_RESPONSE,
        MSG_DATA_PERIODIC,
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const esp_err_t ret = EspNowLink::register_handler(ids[i], business_link_handler);
        if (ret != ESP_OK) {
            for (size_t registered = 0; registered < i; ++registered) {
                EspNowLink::unregister_handler(ids[registered]);
            }
            return ret;
        }
    }
    return ESP_OK;
}

} // namespace

esp_err_t start_business_service() {
    if (business_queue != nullptr) {
        return ESP_OK;
    }
    business_queue = xQueueCreate(BUSINESS_QUEUE_LENGTH, sizeof(BusinessEvent));
    if (business_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = register_business_handlers();
    if (ret != ESP_OK) {
        vQueueDelete(business_queue);
        business_queue = nullptr;
        return ret;
    }
    if (xTaskCreate(business_task,
                    "espnow_business",
                    BUSINESS_TASK_STACK_SIZE,
                    nullptr,
                    BUSINESS_TASK_PRIORITY,
                    &business_task_handle) != pdPASS) {
        EspNowLink::unregister_handler(MSG_SWITCH_REQUEST);
        EspNowLink::unregister_handler(MSG_SWITCH_RESPONSE);
        EspNowLink::unregister_handler(MSG_DATA_REQUEST);
        EspNowLink::unregister_handler(MSG_DATA_RESPONSE);
        EspNowLink::unregister_handler(MSG_DATA_PERIODIC);
        vQueueDelete(business_queue);
        business_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

} // namespace Internal

esp_err_t init() {
    const esp_err_t ret = Internal::start_business_service();
    if (ret == ESP_OK) {
        Internal::register_product_handlers();
    }
    return ret;
}

void set_switch_request_handler(SwitchRequestHandler handler, void* context) {
    Internal::set_callback(&Internal::switch_request_slot, handler, context);
}

void set_switch_response_handler(SwitchResponseHandler handler, void* context) {
    Internal::set_callback(&Internal::switch_response_slot, handler, context);
}

void set_data_request_handler(DataRequestHandler handler, void* context) {
    Internal::set_callback(&Internal::data_request_slot, handler, context);
}

void set_data_received_handler(DataReceivedHandler handler, void* context) {
    Internal::set_callback(&Internal::data_received_slot, handler, context);
}

esp_err_t send_switch_request(const EspNowLink::MacAddress& destination,
                              SwitchAction action,
                              uint32_t* request_id,
                              EspNowLink::SendCallback callback,
                              void* context) {
    if (action != SwitchAction::OFF &&
        action != SwitchAction::ON &&
        action != SwitchAction::TOGGLE) {
        return ESP_ERR_INVALID_ARG;
    }
    Internal::SwitchRequest request = {};
    request.request_id = Internal::next_request_id();
    request.action = action;
    uint8_t payload[5] = {};
    const size_t size = Internal::encode_switch_request(request, payload, sizeof(payload));
    const esp_err_t ret = EspNowLink::send(destination,
                                          Internal::MSG_SWITCH_REQUEST,
                                          payload,
                                          size,
                                          Internal::reliable_options(),
                                          callback,
                                          context);
    if (ret == ESP_OK && request_id != nullptr) {
        *request_id = request.request_id;
    }
    return ret;
}

esp_err_t request_device_data(const EspNowLink::MacAddress& destination,
                              uint32_t* request_id,
                              EspNowLink::SendCallback callback,
                              void* context) {
    const uint32_t id = Internal::next_request_id();
    uint8_t payload[4] = {};
    const size_t size = Internal::encode_data_request(id, payload, sizeof(payload));
    const esp_err_t ret = EspNowLink::send(destination,
                                          Internal::MSG_DATA_REQUEST,
                                          payload,
                                          size,
                                          Internal::reliable_options(),
                                          callback,
                                          context);
    if (ret == ESP_OK && request_id != nullptr) {
        *request_id = id;
    }
    return ret;
}

esp_err_t send_periodic_data(const EspNowLink::MacAddress& destination,
                             const DeviceData& data,
                             EspNowLink::SendCallback callback,
                             void* context) {
    Internal::DataMessage message = {};
    message.available = true;
    message.data = data;
    uint8_t payload[40] = {};
    const size_t size = Internal::encode_data_message(message, payload, sizeof(payload));
    EspNowLink::SendOptions options = {};
    options.delivery = EspNowLink::Delivery::BEST_EFFORT;
    return EspNowLink::send(destination,
                            Internal::MSG_DATA_PERIODIC,
                            payload,
                            size,
                            options,
                            callback,
                            context);
}

} // namespace EspNowService
