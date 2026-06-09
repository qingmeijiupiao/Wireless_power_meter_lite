#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "blackbox.h"
#include "blackbox_service.h"
#include "boot_diagnostics.h"
#include "ulp_loader.h"
#include "HXC_NVS.h"
#include "hardware.h"
#include "TMP235.h"
#include "ESPChipTemperatureSensor.h"
#include "global_state.h"
#include "energy_meter.h"
#include "shell_command.h"
#include "screen.h"
#include "can_callback.h"
#include "power_output.h"
#include "web_backend.h"
#include "ota_service.h"

auto& global_state   = get_global_state();
auto& Chip_Temperature_Sensor = ESPChipTemperatureSensor_t::instance();
auto& Board_Temperature_sensor = TMP235_t::instance();

static constexpr char TAG[] = "app_main";

void update_main_state(TimerHandle_t xTimer){
    LP_Core_Snapshot snapshot = {};
    if (!LP_Core_GetSnapshot(&snapshot)) {
        return;
    }
    global_state.voltage_mV = snapshot.voltage_uv/1e3;
    global_state.current_uA = snapshot.current_uA;
    // GlobalState 保存固定大小的展示值；精确累计值交给 energy_meter 用于会话差分。
    global_state.meter_mah = snapshot.meter_uah / 1000.0f;
    global_state.meter_mwh = snapshot.meter_uwh / 1000.0f;
    EnergyMeter::update_lifetime(snapshot.meter_uah, snapshot.meter_uwh);
    global_state.current_register_raw = snapshot.shunt_register_raw;
    global_state.voltage_register_raw = snapshot.voltage_register_raw;
    global_state.board_temperature = Board_Temperature_sensor.getTemperature();
    global_state.chip_temperature = Chip_Temperature_Sensor.getTemperature()*100.0f;
    LP_Core_SetBoardTemperature(global_state.board_temperature);
    global_state.flags.bits.lp_core_running = snapshot.state.ulp_state_bits.ulp_run;
    global_state.flags.bits.lp_ina226_initialized = snapshot.state.ulp_state_bits.ulp_ina226_init_ok;
    global_state.flags.bits.lp_i2c_error = snapshot.state.ulp_state_bits.ulp_i2c_init_err;
    global_state.flags.bits.lp_ina226_read_timeout = snapshot.state.ulp_state_bits.ulp_ina226_read_timeout;
}


extern "C" void app_main(void){
    ESP_ERROR_CHECK(Blackbox::init());
    global_state.flags.bits.blackbox_enabled = Blackbox::is_enabled();
    ESP_ERROR_CHECK(HXC::NVS_Base::setup());
    ESP_ERROR_CHECK(BlackboxService::init());
    ESP_ERROR_CHECK(OtaService::init());

    BootDiagnostics::append_stage("hardware_config_init");
    esp_err_t hardware_ret = hardware_config_init();
    if (hardware_ret != ESP_OK) {
        BootDiagnostics::append_hardware_config_failure(hardware_ret);
        ESP_LOGE(TAG, "hardware config detection failed, using default config: %s",
                 esp_err_to_name(hardware_ret));
    }

    BootDiagnostics::append_system_boot_start();
    BootDiagnostics::append_early();

    esp_err_t ret = Chip_Temperature_Sensor.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "chip temperature sensor unavailable: %s", esp_err_to_name(ret));
    }
    ret = Board_Temperature_sensor.init(get_hardware_config().temperature_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "board temperature sensor unavailable: %s", esp_err_to_name(ret));
    }
    TimerHandle_t xMyTimer = xTimerCreate("update_main_state", pdMS_TO_TICKS(5), pdTRUE, NULL, update_main_state);
    configASSERT(xMyTimer != nullptr);
    configASSERT(xTimerStart(xMyTimer, 0) == pdPASS);
    configASSERT(xTaskCreate(SCREEN::screen_task, "screen_task", 4096, NULL, 4, NULL) == pdPASS);

    BootDiagnostics::append_stage("lp_core_load");
    ret = LP_Core_Load();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LP core unavailable: %s", esp_err_to_name(ret));
    }
    ESP_ERROR_CHECK(protect_init());

    ESP_ERROR_CHECK(PowerOutput::init(get_hardware_config().OUTPUT_CTRL));

    ESP_ERROR_CHECK(SCREEN::init_buttons());

    ESP_ERROR_CHECK(CanCallback::init());

    ESP_ERROR_CHECK(ShellCommand::init());

    BootDiagnostics::append_stage("wifi_web_start");
    WebBackend::start_with_wifi_service();
    BootDiagnostics::append_runtime();

    while (1){
        // Main loop only use for debug
        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
    
}
