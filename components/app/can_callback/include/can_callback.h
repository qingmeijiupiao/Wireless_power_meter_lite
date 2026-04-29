#ifndef CAN_CALLBACK_H
#define CAN_CALLBACK_H

#include "esp_err.h"
#include "HXC_TWAI.h"
#include "HXC_NVS.h"

enum CAN_CALLBACK_ID : uint8_t {
    CAN_CALLBACK_PING=0x00,
    CAN_CALLBACK_GET_STATE=0x01,
    CAN_CALLBACK_SET_OUTPUT=0x02,
};

namespace CanCallback {

constexpr uint32_t DEFAULT_CAN_BAUDRATE = 1_Mbps;
constexpr uint32_t DEFAULT_DEVICE_CAN_ID = 0x400;//默认设备ID 设备ID大于0x700时使用扩展帧，否则使用标准帧
extern HXC::NVS_DATA<uint32_t> CAN_BAUDRATE;
extern HXC::NVS_DATA<uint32_t> CAN_ID;

esp_err_t init();

HXC_TWAI& get_can_bus();


}

#endif
