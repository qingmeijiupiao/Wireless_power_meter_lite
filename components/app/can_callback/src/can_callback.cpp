#include "can_callback.h"
#include "esp_log.h"
#include "hardware.h"
#include <cstdio>
#include "global_state.h"
#include "power_output.h"
namespace CanCallback {

static const char* TAG = "CanCallback";

static HXC_TWAI* can_bus = nullptr;

HXC::NVS_DATA<uint32_t> CAN_BAUDRATE("CAN_BAUDRATE", DEFAULT_CAN_BAUDRATE);
HXC::NVS_DATA<uint32_t> CAN_ID("CAN_ID", DEFAULT_DEVICE_CAN_ID);
CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> can_register;

HXC_TWAI& get_can_bus() {
    return *can_bus;
}

esp_err_t init() {
    auto& hw = get_hardware_config();

    ESP_ERROR_CHECK(can_register.init(hw.CAN_RESISTOR_ENABLE));
    can_register.set(false);

    can_bus = new HXC_TWAI(hw.CAN_TX, hw.CAN_RX, 1_Mbps);
    ESP_ERROR_CHECK(can_bus->setup());

    // ====== 回调列表 ======

    /**
     * @brief  PING 回调
     * @action PING 回调，返回 PING 帧
     */
    can_bus->add_can_receive_callback_func(CAN_ID+CALLBACK_PING, [](HXC_CAN_message_t* msg) {
        can_bus->send(msg);
    });

    /**
     * @brief  获取状态 回调
     * @action 获取状态 回调，返回状态数据
     */
    can_bus->add_can_receive_callback_func(CAN_ID+CALLBACK_GET_STATE, [](HXC_CAN_message_t* msg) {
        static HXC_CAN_message_t state_msg ={};

        auto& state = get_global_state();
        CALLBACK_GET_STATE_DATA_t state_data ={};
        state_data.voltage_mV        = state.voltage_mV;
        state_data.current_mA        = std::abs(state.current_uA/1000);
        state_data.Board_temperature = state.board_temperature/100;
        state_data.Chip_temperature  = state.chip_temperature/100;
        state_data.output_state      = state.global_state_bits.state_bit.out_put_state;
        state_data.current_direction = state.current_uA > 0 ? 1 : 0;
        state_data.UVP_flag          = state.protect_states.states_bit.low_voltage_protect_state;
        state_data.OVP_flag          = state.protect_states.states_bit.high_voltage_protect_state;
        state_data.OTP_flag          = state.protect_states.states_bit.temperature_protect_state;
        state_data.OCP_flag          = state.protect_states.states_bit.current_protect_state;

        if(CAN_ID+CALLBACK_GET_STATE > 0x7ff){
            state_msg.extd = true;
        }else{
            state_msg.extd = false;
        }

        state_msg.identifier = CAN_ID+CALLBACK_GET_STATE;
        state_msg.data_length_code = sizeof(CALLBACK_GET_STATE_DATA_t);
        memcpy(state_msg.data, &state_data, sizeof(CALLBACK_GET_STATE_DATA_t));
        can_bus->send(&state_msg);
        
    });

    /**
     * @brief  设置输出 回调
     * @action 设置输出 回调，根据输出状态设置输出引脚
     */
    can_bus->add_can_receive_callback_func(CAN_ID+CALLBACK_SET_OUTPUT, [](HXC_CAN_message_t* msg) {
        ESP_LOGI(TAG, "Setting output");
        if (msg->data[0] == 0x01) {
            PowerOutput::on();
        } else {
            PowerOutput::off();
        }
    });

    /**
     * @brief  -1 - 测试回调
     * @usage  收到任意 CAN 帧时打印内容
     * @note   仅用于调试，生产环境请移除
     */
    can_bus->add_can_receive_callback_func(-1, [](HXC_CAN_message_t* msg) {
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
