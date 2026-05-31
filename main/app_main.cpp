#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_log.h"

#include "blackbox.h"
#include "blackbox_service.h"
#include "ulp_loader.h"
#include "HXC_NVS.h"
#include "hardware.h"
#include "TMP235.h"
#include "ESPChipTemperatureSensor.h"
#include "global_state.h"
#include "Button.h"
#include "shell_command.h"
#include "screen.h"
#include "pwm.h"
#include "can_callback.h"
#include "power_output.h"
#include "web_backend.h"

auto& shell_instance = Shell::instance();

Button Main_Button;
Button Side_Button;
pwm_t blk;

auto& global_state   = get_global_state();
auto& Chip_Temperature_Sensor = ESPChipTemperatureSensor_t::instance();
auto& Board_Temperature_sensor = TMP235_t::instance();


void update_main_state(TimerHandle_t xTimer){
    global_state.voltage_mV = ulp_voltage_uv/1e3;
    global_state.current_uA = ulp_current_uA;
    global_state.meter_uah = ulp_meter_uah;
    global_state.meter_uwh = ulp_meter_uwh;
    global_state.board_temperature = Board_Temperature_sensor.getTemperature();
    global_state.chip_temperature = Chip_Temperature_Sensor.getTemperature()*100.0f;
    *(int32_t*)&(ulp_Board_temperature) = global_state.board_temperature;
    global_state.flags.bits.lp_core_running = ulp_state.ulp_state_bits.ulp_run;
    global_state.flags.bits.lp_ina226_initialized = ulp_state.ulp_state_bits.ulp_ina226_init_ok;
    global_state.flags.bits.lp_i2c_error = ulp_state.ulp_state_bits.ulp_i2c_init_err;
    global_state.flags.bits.lp_ina226_read_timeout = ulp_state.ulp_state_bits.ulp_ina226_read_timeout;
}


void OUTPUT_ctrl(){
    if (!SCREEN::post_button_event(SCREEN::ButtonId::Main, ButtonEvent::SHORT_PRESS)) {
        PowerOutput::toggle();
    }
}



extern "C" void app_main(void){
    ESP_ERROR_CHECK(hardware_config_init());
    ESP_ERROR_CHECK(Blackbox::init());
    global_state.flags.bits.blackbox_enabled = Blackbox::is_enabled();
    HXC::NVS_Base::setup();
    ESP_ERROR_CHECK(BlackboxService::init());
    const esp_app_desc_t* app_desc = esp_app_get_description();
    BlackboxService::append_event("system: boot_start fw=%s build=%s_%s hw_version=%u",
                                  app_desc->version,
                                  app_desc->date,
                                  app_desc->time,
                                  static_cast<unsigned>(get_hardware_version()));

    ESP_ERROR_CHECK(Chip_Temperature_Sensor.init());
    ESP_ERROR_CHECK(Board_Temperature_sensor.init(get_hardware_config().temperature_channel));
    TimerHandle_t xMyTimer = xTimerCreate("update_main_state", pdMS_TO_TICKS(5), pdTRUE, NULL, update_main_state); xTimerStart( xMyTimer, 0 );
    xTaskCreate(SCREEN::screen_task, "screen_task", 4096, NULL, 4, NULL);

    LP_Core_Load();
    ESP_ERROR_CHECK(protect_init());

    ESP_ERROR_CHECK(PowerOutput::init(get_hardware_config().OUTPUT_CTRL));

    Main_Button.bind_event(ButtonEvent::SHORT_PRESS, OUTPUT_ctrl);
    Side_Button.bind_event(ButtonEvent::SHORT_PRESS, [](){
        SCREEN::post_button_event(SCREEN::ButtonId::Side, ButtonEvent::SHORT_PRESS);
    });
    Side_Button.bind_event(ButtonEvent::DOUBLE_CLICK, [](){
        SCREEN::post_button_event(SCREEN::ButtonId::Side, ButtonEvent::DOUBLE_CLICK);
    });
    Side_Button.bind_event(ButtonEvent::LONG_PRESS, [](){
        SCREEN::post_button_event(SCREEN::ButtonId::Side, ButtonEvent::LONG_PRESS);
    });
    Side_Button.bind_event(ButtonEvent::SUPER_LONG_PRESS, [](){
        SCREEN::post_button_event(SCREEN::ButtonId::Side, ButtonEvent::SUPER_LONG_PRESS);
    });
    ESP_ERROR_CHECK(Main_Button.setup(get_hardware_config().MAIN_BUTTON, true));
    ESP_ERROR_CHECK(Side_Button.setup(get_hardware_config().SIDE_BUTTON, true));

    ESP_ERROR_CHECK(CanCallback::init());

    ESP_ERROR_CHECK(ShellCommand::init());

    WebBackend::start_with_wifi_service();
    BlackboxService::append_event("system: app_ready");

    while (1){
        // Main loop only use for debug
        //ESP_LOGI("app_main", "ina226_run register: %d %d", *current_register_raw, *voltage_register_raw);

        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
    
}
