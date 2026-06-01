#include "can_callback.h"
#include "can_resistor.h"
#include "esp_log.h"
#include "hardware.h"
#include <cstdio>
#include "global_state.h"
#include "power_output.h"
#include "blackbox_service.h"
namespace CanCallback {

static constexpr char TAG[] = "CanCallback";

static HXC_TWAI* can_bus = nullptr;

HXC::NVS_DATA<uint32_t> CAN_BAUDRATE("CAN_BAUDRATE", DEFAULT_CAN_BAUDRATE);
HXC::NVS_DATA<uint32_t> CAN_ID("CAN_ID", DEFAULT_DEVICE_CAN_ID);

static void diagnostics_task(void*) {
    uint32_t last_tx_failed = 0;
    uint32_t last_bus_off = 0;
    uint32_t last_bus_error = 0;
    uint32_t last_rx_overflow = 0;
    while (true) {
        const uint32_t tx_failed = can_bus->get_tx_failed_count();
        const uint32_t bus_off = can_bus->get_bus_off_count();
        const uint32_t bus_error = can_bus->get_bus_error_count();
        const uint32_t rx_overflow = can_bus->get_rx_overflow_count();
        if (tx_failed != last_tx_failed || bus_off != last_bus_off ||
            bus_error != last_bus_error || rx_overflow != last_rx_overflow) {
            twai_node_status_t status = {};
            twai_node_record_t statistics = {};
            const esp_err_t ret = can_bus->get_info(&status, &statistics);
            BlackboxService::append_event("can: diagnostics info=%s state=%u tx_err=%u rx_err=%u bus_err=%lu bus_off=%lu tx_failed=%lu rx_overflow=%lu",
                                          esp_err_to_name(ret),
                                          static_cast<unsigned>(status.state),
                                          static_cast<unsigned>(status.tx_error_count),
                                          static_cast<unsigned>(status.rx_error_count),
                                          static_cast<unsigned long>(statistics.bus_err_num),
                                          static_cast<unsigned long>(bus_off),
                                          static_cast<unsigned long>(tx_failed),
                                          static_cast<unsigned long>(rx_overflow));
            last_tx_failed = tx_failed;
            last_bus_off = bus_off;
            last_bus_error = bus_error;
            last_rx_overflow = rx_overflow;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

HXC_TWAI& get_can_bus() {
    return *can_bus;
}

esp_err_t init() {
    auto& hw = get_hardware_config();
    auto& can_resistor = CanResistor::instance();

    ESP_ERROR_CHECK(can_resistor.init(hw.CAN_RESISTOR_ENABLE));

    
    can_bus = new HXC_TWAI(hw.CAN_TX, hw.CAN_RX, CAN_BAUDRATE.read());
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
        state_data.output_state      = state.flags.bits.output_enabled;
        state_data.current_direction = state.current_uA > 0 ? 1 : 0;
        state_data.CAN_resistor      = CanResistor::instance().get();
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
        PowerOutput::OutputResult result;
        if (msg->data[0] == 0x01) {
            result = PowerOutput::on(TAG);
        } else {
            result = PowerOutput::off(TAG);
        }
        BlackboxService::append_event("can: set_output target=%u result=%u",
                                      msg->data[0] == 0x01 ? 1U : 0U,
                                      static_cast<unsigned>(result));
    });
    /**
     * @brief  设置终端电阻 回调
     * @action 设置终端电阻 回调，根据终端电阻状态设置终端电阻引脚
     */
    can_bus->add_can_receive_callback_func(CAN_ID+CALLBACK_SET_RESISTOR, [](HXC_CAN_message_t* msg) {
        ESP_LOGI(TAG, "Setting CAN resistor");
        const bool enabled = msg->data[0] == 0x01;
        const esp_err_t ret = CanResistor::instance().set(enabled);
        BlackboxService::append_event("can: set_resistor target=%u result=%s",
                                      enabled ? 1U : 0U,
                                      esp_err_to_name(ret));
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
    BlackboxService::append_event("can: init id=0x%lx baud=%lu resistor=%u",
                                  static_cast<unsigned long>(CAN_ID.read()),
                                  static_cast<unsigned long>(CAN_BAUDRATE.read()),
                                  can_resistor.get() ? 1U : 0U);
    if (xTaskCreate(diagnostics_task, "can_diag", 3072, nullptr, 2, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "failed to create diagnostics task");
    }
    return ESP_OK;
}

}
