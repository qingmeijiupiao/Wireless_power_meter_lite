#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "blackbox.h"
#include "ulp_loader.h"
#include "HXC_NVS.h"
#include "hardware.h"
#include "cpp_gpio_driver.hpp"
#include "TMP235.h"
#include "ESPChipTemperatureSensor.h"
#include "HXC_TWAI.h"
#include "global_state.h"
#include "Button.h"
#include "shell_command.h"
#include "screen.h"
#include "wifi_manager.h"
#include "pwm.h"

auto& shell_instance = Shell::instance();

auto& wifi_manager = WiFiManager::instance();

CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> POWER_OUT;
CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> CAN_register;
Button Main_Button; // 主按键
Button Side_Button; // 侧边上的按键
pwm_t blk;

auto& global_state   = get_global_state();
auto& protect_states = global_state.protect_states.states_bit;

HXC_TWAI CAN_BUS(18,14,1_Mbps);

auto& Chip_Temperature_Sensor = ESPChipTemperatureSensor_t::instance();
auto& Board_Temperature_sensor = TMP235_t::instance();


void update_main_state(TimerHandle_t xTimer){
    global_state.voltage_mV = ulp_voltage_uv/1e3;
    global_state.current_uA = ulp_current_uA;
    global_state.board_temperature = Board_Temperature_sensor.getTemperature();
    global_state.chip_temperature = Chip_Temperature_Sensor.getTemperature()*100.0f;
}


void OUTPUT_ctrl(){
    // 保护状态判断, 有保护状态时, 输出关闭
    if(have_protect()){
        ESP_LOGW("OUTPUT_ctrl", "has_protect disable to output");
        global_state.global_state_bits.state_bit.out_put_state = false;
    }else{
        ESP_LOGI("OUTPUT_ctrl", "button toggle output");
        global_state.global_state_bits.state_bit.out_put_state = !global_state.global_state_bits.state_bit.out_put_state;
    }

    POWER_OUT.set(global_state.global_state_bits.state_bit.out_put_state);
}

void test_callback(HXC_CAN_message_t* can_message){
    ESP_LOGI("HXC_TWAI", "ID=%08lX", can_message->identifier);
}

void CAN_test_task(void* arg){
    CAN_register.set(true);
    ESP_ERROR_CHECK(CAN_BUS.setup());

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    CAN_BUS.add_can_receive_callback_func(0x123,test_callback);
    HXC_CAN_message_t send_message={};
    send_message.identifier = 0x123;
    send_message.data_length_code = 8;
    for(int i=0;i<8;i++){
        send_message.data[i] = i;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    uint8_t send_count = 0;
    while (1){
        ESP_LOGI("HXC_TWAI", "send_count: %d", send_count++);
        auto ret = CAN_BUS.send(&send_message);
        if(ret != ESP_OK){
            ESP_LOGE("HXC_TWAI", "CAN send error: %s", esp_err_to_name(ret));
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}


extern "C" void app_main(void){
    ESP_ERROR_CHECK(hardware_config_init());
    ESP_ERROR_CHECK(BlackBox::init());
    HXC::NVS_Base::setup();

    ESP_ERROR_CHECK(Chip_Temperature_Sensor.init());
    ESP_ERROR_CHECK(Board_Temperature_sensor.init(get_hardware_config().temperature_channel));
    TimerHandle_t xMyTimer = xTimerCreate("update_main_state", pdMS_TO_TICKS(5), pdTRUE, NULL, update_main_state); xTimerStart( xMyTimer, 0 );
    xTaskCreate(SCREEN::screen_task, "screen_task", 4096, NULL, 4, NULL);

    LP_Core_Load();
    ESP_ERROR_CHECK(protect_init());
    add_on_protect_change_callback([](ProtectState_t last_state, ProtectState_t new_state){
        ESP_LOGI("protect_callback", "protect state changed: %d -> %d", last_state, new_state);
        if(new_state == PROTECT_STATE_PROTECT){
            OUTPUT_ctrl();
        }
    });


    Main_Button.bind_event(ButtonEvent::SHORT_PRESS, OUTPUT_ctrl);
    Side_Button.bind_event(ButtonEvent::SHORT_PRESS, [](){
        ESP_LOGI("Side_Button", "button toggle");
    });
    ESP_ERROR_CHECK(Main_Button.setup(get_hardware_config().MAIN_BUTTON, true));
    ESP_ERROR_CHECK(Side_Button.setup(get_hardware_config().SIDE_BUTTON, true));
    ESP_ERROR_CHECK(CAN_register.init(get_hardware_config().CAN_RESISTOR_ENABLE));
    ESP_ERROR_CHECK(POWER_OUT.init(get_hardware_config().OUTPUT_CTRL));
    POWER_OUT.set(false);

    ESP_ERROR_CHECK(ShellCommand::init());



    // xTaskCreate(CAN_test_task, "CAN_test_task", 8192, NULL, 6, NULL);

    // ESP_LOGI("app_main", "CAN_test_task started");
 
    // wifi_manager.init();

    while (1){
        // Main loop only use for debug
        //ESP_LOGI("app_main", "ina226_run state: %d");

        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
    
}