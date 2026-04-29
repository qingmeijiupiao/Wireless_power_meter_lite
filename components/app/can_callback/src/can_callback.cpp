#include "can_callback.h"
#include "esp_log.h"
#include "hardware.h"
#include "cpp_gpio_driver.hpp"
#include <cstdio>

namespace CanCallback {

static const char* TAG = "CanCallback";

static HXC_TWAI* can_bus = nullptr;

HXC::NVS_DATA<uint32_t> CAN_BAUDRATE("CAN_BAUDRATE", DEFAULT_CAN_BAUDRATE);
HXC::NVS_DATA<uint32_t> CAN_ID("CAN_ID", DEFAULT_DEVICE_CAN_ID);

HXC_TWAI& get_can_bus() {
    return *can_bus;
}

// ====== 回调列表 ======

esp_err_t init() {
    auto& hw = get_hardware_config();

    static CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> can_register;
    ESP_ERROR_CHECK(can_register.init(hw.CAN_RESISTOR_ENABLE));
    can_register.set(true);

    can_bus = new HXC_TWAI(hw.CAN_TX, hw.CAN_RX, 1_Mbps);
    ESP_ERROR_CHECK(can_bus->setup());

    /**
     * @brief  PING 回调
     * @action PING 回调，返回 PING 帧
     */
    can_bus->add_can_receive_callback_func(CAN_ID+CAN_CALLBACK_PING, [](HXC_CAN_message_t* msg) {
        can_bus->send(msg);
    });

    /**
     * @brief  0x123 - 测试回调
     * @usage  收到 ID=0x123 的 CAN 帧时打印内容
     * @note   仅用于调试，生产环境请移除
     */
    can_bus->add_can_receive_callback_func(0x123, [](HXC_CAN_message_t* msg) {
        ESP_LOGI(TAG, "ID=%08lX DLC=%d", msg->identifier, msg->data_length_code);
        printf("Data: ");
        for (int i = 0; i < msg->data_length_code; i++) {
            printf("%02X ", msg->data[i]);
        }
        printf("\n");
    });

    // --- 添加新回调模板 ---
    // /**
    //  * @brief  0x<ID> - <简要描述>
    //  * @usage  收到 ID=0x<ID> 的 CAN 帧时执行
    //  * @param  msg - CAN 消息指针
    //  * @note   <注意事项>
    //  */
    // can_bus->add_can_receive_callback_func(0x<ID>,
    //     [](HXC_CAN_message_t* msg) {
    //         // 回调实现
    //     });

    ESP_LOGI(TAG, "CAN initialized and callbacks registered");
    return ESP_OK;
}

}
