#include "HXC_TWAI.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"

struct twai_node_map_t{
    twai_node_handle_t handle;
    HXC_TWAI* twai;
};
static twai_node_map_t twai_node_maps[3];

HXC_TWAI::HXC_TWAI(uint8_t tx, uint8_t rx, CAN_RATE rate): TX_PIN(tx), RX_PIN(rx), can_rate(rate){
    // 初始化回调函数数组
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        callback_maps[i].used = false;
        callback_maps[i].addr = 0;
    }
    callback_count = 0;
}

HXC_TWAI::~HXC_TWAI() {
    // if (twai_node_handle != nullptr) {
    //     twai_stop_v2(twai_node_handle);
    //     twai_driver_uninstall_v2(twai_node_handle);
    // }
}
uint32_t test_count = 0;
void HXC_TWAI::receive_callback(){
    if(have_new_receive){
        have_new_receive = false;
        // 线性查表查找对应的回调函数
        for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
            if (callback_maps[i].used && callback_maps[i].addr == RX_message_buf.identifier) {
                callback_maps[i].func(&RX_message_buf);
                break;
            }
        }
        test_count++;
    }

}


bool HXC_TWAI::on_rx_done_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx){
    uint8_t recv_buff[8];
    twai_frame_t rx_frame;
    rx_frame.buffer = recv_buff;
    rx_frame.buffer_len = sizeof(recv_buff);
    for (size_t i = 0; i < sizeof(twai_node_maps)/sizeof(twai_node_maps[0]); i++){
        if(twai_node_maps[i].handle == handle){
            if (ESP_OK != twai_node_receive_from_isr(handle, &rx_frame)){
                return ESP_FAIL;
            }
            twai_node_maps[i].twai->RX_message_buf.identifier = rx_frame.header.id;
            twai_node_maps[i].twai->RX_message_buf.data_length_code = rx_frame.header.dlc;
            twai_node_maps[i].twai->RX_message_buf.rtr = rx_frame.header.rtr;
            twai_node_maps[i].twai->RX_message_buf.extd = rx_frame.header.ide;
            memcpy(twai_node_maps[i].twai->RX_message_buf.data, rx_frame.buffer, rx_frame.buffer_len);
            twai_node_maps[i].twai->have_new_receive = true;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t HXC_TWAI::setup() {
    if (twai_node_handle != nullptr) {// 如果已经初始化过，不再初始化   
        return ESP_OK;
    }

    for (size_t i = 0; i < sizeof(twai_node_maps)/sizeof(twai_node_maps[0]); i++){
        if(twai_node_maps[i].handle == nullptr){
            twai_node_maps[i].handle = twai_node_handle;
            twai_node_maps[i].twai = this;
            break;
        }
    }

    twai_onchip_node_config_t node_config={};
    node_config.io_cfg.tx = static_cast<gpio_num_t>(TX_PIN);
    node_config.io_cfg.rx = static_cast<gpio_num_t>(RX_PIN);
    node_config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    node_config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
    node_config.tx_queue_depth = 5;
    node_config.intr_priority = 1;
    node_config.bit_timing = {};
    node_config.clk_src = TWAI_CLK_SRC_DEFAULT;
    node_config.timestamp_resolution_hz = 0;
    node_config.fail_retry_cnt = 5;
    node_config.flags.enable_self_test = false;
    node_config.flags.enable_loopback = false;
    node_config.flags.enable_listen_only = false;
    node_config.flags.no_receive_rtr = false;

    switch (can_rate){
        case CAN_RATE::CAN_RATE_1MBIT:
            node_config.bit_timing.bitrate = 1000000;
            break;
        case CAN_RATE::CAN_RATE_800KBIT:
            node_config.bit_timing.bitrate = 800000;
            break;
        case CAN_RATE::CAN_RATE_500KBIT:
            node_config.bit_timing.bitrate = 500000;
            break;
        case CAN_RATE::CAN_RATE_250KBIT:
            node_config.bit_timing.bitrate = 250000;
            break;
        case CAN_RATE::CAN_RATE_125KBIT:
            node_config.bit_timing.bitrate = 125000;
            break;
        case CAN_RATE::CAN_RATE_100KBIT:
            node_config.bit_timing.bitrate = 100000;
            break;
        default:
            node_config.bit_timing.bitrate = 1000000;
    }
    esp_err_t ret = twai_new_node_onchip(&node_config, &twai_node_handle);
    if(ret != ESP_OK){
        return ret;
    }

    while (twai_node_handle == nullptr){
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    for (size_t i = 0; i < sizeof(twai_node_maps)/sizeof(twai_node_maps[0]); i++){
        if(twai_node_maps[i].handle == nullptr){
            twai_node_maps[i].handle = twai_node_handle;
            twai_node_maps[i].twai = this;
            break;
        }
    }
    have_new_receive = false;
    twai_event_callbacks_t user_cbs={};
    user_cbs.on_rx_done = on_rx_done_callback;
    ret = twai_node_register_event_callbacks(twai_node_handle, &user_cbs,NULL);
    if(ret != ESP_OK){
        return ret;
    }

    // 启动CAN驱动
    ret = twai_node_enable(twai_node_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    xTaskCreate(receive_task, "receive_task", 4096, this, 2, NULL);
    return ESP_OK;
}

// 添加CAN消息接收回调函数
void HXC_TWAI::add_can_receive_callback_func(int addr, HXC_can_feedback_func func) {
    // 查找是否已存在相同地址的回调
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == addr) {
            callback_maps[i].func = func;  // 更新现有回调
            return;
        }
    }
    
    // 查找空闲位置添加新回调
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (!callback_maps[i].used) {
            callback_maps[i].addr = addr;
            callback_maps[i].func = func;
            callback_maps[i].used = true;
            callback_count++;
            return;
        }
    }
    // 如果数组已满，无法添加新回调
}

// 移除CAN消息接收回调函数
void HXC_TWAI::remove_can_receive_callback_func(int addr) {
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == addr) {
            callback_maps[i].used = false;
            callback_maps[i].addr = 0;
            callback_count--;
            return;
        }
    }
}

// 判断CAN消息接收回调函数是否存在
bool HXC_TWAI::exist_can_receive_callback_func(int addr) {
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == addr) {
            return true;
        }
    }
    return false;
}

// 获取初始化状态
bool HXC_TWAI::get_setup_flag() {
    return twai_node_handle != nullptr;
}

esp_err_t HXC_TWAI::send(HXC_CAN_message_t* message) {
    if(twai_node_handle == nullptr || message == nullptr){
        return ESP_FAIL;
    }
    tx_message_head_buf.header.ide = message->extd;
    tx_message_head_buf.header.rtr = message->rtr;
    tx_message_head_buf.header.id = message->identifier;
    tx_message_head_buf.header.dlc = message->data_length_code;
    for (size_t i = 0; i < message->data_length_code; i++){
        tx_message_data_buf[i] = message->data[i];
    }
    tx_message_head_buf.buffer = tx_message_data_buf;
    tx_message_head_buf.buffer_len = message->data_length_code;
    auto ret = twai_node_transmit(twai_node_handle, &tx_message_head_buf,100);
    return ret;
}

void HXC_TWAI::receive_task(void* arg){
    HXC_TWAI* twai = reinterpret_cast<HXC_TWAI*>(arg);
    while(true){
        twai->receive_callback();
        vTaskDelay(1);
    }

}