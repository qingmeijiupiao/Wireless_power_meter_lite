#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "blackbox.h"
#include "ulp_loader.h"
#include "HXC_NVS.h"
#include "hardware.h"
#include "TMP235.h"
#include "ESPChipTemperatureSensor.h"
#include "global_state.h"
#include "Button.h"
#include "shell_command.h"
#include "screen.h"
#include "wifi_manager.h"
#include "pwm.h"
#include "can_callback.h"
#include "power_output.h"

auto& shell_instance = Shell::instance();

auto& wifi_manager = WiFiManager::instance();

Button Main_Button;
Button Side_Button;
pwm_t blk;

auto& global_state   = get_global_state();
auto& protect_states = global_state.protect_states.states_bit;

auto& Chip_Temperature_Sensor = ESPChipTemperatureSensor_t::instance();
auto& Board_Temperature_sensor = TMP235_t::instance();


void update_main_state(TimerHandle_t xTimer){
    global_state.voltage_mV = ulp_voltage_uv/1e3;
    global_state.current_uA = ulp_current_uA;
    global_state.board_temperature = Board_Temperature_sensor.getTemperature();
    global_state.chip_temperature = Chip_Temperature_Sensor.getTemperature()*100.0f;
}


void OUTPUT_ctrl(){
    PowerOutput::toggle();
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

    ESP_ERROR_CHECK(PowerOutput::init(get_hardware_config().OUTPUT_CTRL));

    Main_Button.bind_event(ButtonEvent::SHORT_PRESS, OUTPUT_ctrl);
    Side_Button.bind_event(ButtonEvent::SHORT_PRESS, [](){
        ESP_LOGI("Side_Button", "button toggle");
    });
    ESP_ERROR_CHECK(Main_Button.setup(get_hardware_config().MAIN_BUTTON, true));
    ESP_ERROR_CHECK(Side_Button.setup(get_hardware_config().SIDE_BUTTON, true));

    ESP_ERROR_CHECK(CanCallback::init());

    ESP_ERROR_CHECK(ShellCommand::init());

    // wifi_manager.init();

    while (1){
        // Main loop only use for debug
        //ESP_LOGI("app_main", "ina226_run state: %d");

        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
    
}
