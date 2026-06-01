#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdarg>
#include <cstdio>
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"

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
#include "can_resistor.h"
#include "current_calibration.h"
#include "power_output.h"
#include "web_backend.h"
#include "wifi_service.h"

static constexpr char TAG[] = "app_main";

auto& shell_instance = Shell::instance();

Button Main_Button;
Button Side_Button;
pwm_t blk;

auto& global_state   = get_global_state();
auto& Chip_Temperature_Sensor = ESPChipTemperatureSensor_t::instance();
auto& Board_Temperature_sensor = TMP235_t::instance();

static void append_boot_line(const char* fmt, ...) {
    char text[Blackbox::TEXT_BUFFER_SIZE] = {};
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    BlackboxService::append_text_event("%s", text);
    Blackbox::sync();
}

static void append_early_boot_diagnostics() {
    const esp_app_desc_t* app_desc = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    uint8_t sta_mac[6] = {};
    uint8_t ap_mac[6] = {};
    esp_read_mac(sta_mac, ESP_MAC_WIFI_STA);
    esp_read_mac(ap_mac, ESP_MAC_WIFI_SOFTAP);
    const auto wifi = WifiService::get_config();
    const auto calibration = CurrentCalib::params_data.read();

    append_boot_line("boot: reset_reason=%u fw=%s build=%s_%s hw=%u",
                                  static_cast<unsigned>(esp_reset_reason()),
                                  app_desc->version,
                                  app_desc->date,
                                  app_desc->time,
                                  static_cast<unsigned>(get_hardware_version()));
    append_boot_line("boot: flash_bytes=%lu ota_label=%s ota_subtype=%u",
                                  static_cast<unsigned long>(flash_size),
                                  running == nullptr ? "unknown" : running->label,
                                  running == nullptr ? 0U : static_cast<unsigned>(running->subtype));
    append_boot_line("boot: mac_sta=%02X:%02X:%02X:%02X:%02X:%02X mac_ap=%02X:%02X:%02X:%02X:%02X:%02X",
                                  sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5],
                                  ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5]);
    append_boot_line("boot: can_id=0x%lx can_baud=%lu",
                                  static_cast<unsigned long>(CanCallback::CAN_ID.read()),
                                  static_cast<unsigned long>(CanCallback::CAN_BAUDRATE.read()));
    append_boot_line("boot: wifi_boot=%u saved_ssid=%s",
                                  wifi.web_enabled_on_boot ? 1U : 0U,
                                  wifi.ssid[0] == '\0' ? "(none)" : wifi.ssid);
    append_boot_line("boot: calib base_k=%u temperature_k=%d",
                                  static_cast<unsigned>(calibration.current_base_K),
                                  calibration.temperature_K);
    for (size_t i = 0; i < sizeof(calibration.points) / sizeof(calibration.points[0]); ++i) {
        append_boot_line("boot: calib_point index=%u reg=%d offset_100ua=%d",
                                      static_cast<unsigned>(i),
                                      calibration.points[i].register_value,
                                      calibration.points[i].offset_current_100uA);
    }
    for (uint8_t i = 0; i < protect_get_channel_count(); ++i) {
        protect_channel_info_t info = {};
        if (protect_get_channel_info(i, &info)) {
            append_boot_line("boot: protect channel=%s warn_milli=%ld warn_rec_milli=%ld protect_milli=%ld protect_rec_milli=%ld",
                                          info.name,
                                          static_cast<long>(info.threshold.warning_threshold * 1000.0f),
                                          static_cast<long>(info.threshold.warning_recovery_threshold * 1000.0f),
                                          static_cast<long>(info.threshold.protect_threshold * 1000.0f),
                                          static_cast<long>(info.threshold.protect_recovery_threshold * 1000.0f));
        }
    }
}

static void append_runtime_diagnostics() {
    const IP_t ip = WifiService::get_ip();
    append_boot_line("boot: runtime can_resistor=%u wifi_mode=%u ip=%u.%u.%u.%u",
                                  CanResistor::instance().get() ? 1U : 0U,
                                  static_cast<unsigned>(WifiService::get_mode()),
                                  ip.octet1,
                                  ip.octet2,
                                  ip.octet3,
                                  ip.octet4);
    append_boot_line("boot: runtime ina226_raw_i=%d ina226_raw_v=%u flags=0x%lx",
                                  current_register_raw == nullptr ? 0 : *current_register_raw,
                                  voltage_register_raw == nullptr ? 0U : *voltage_register_raw,
                                  static_cast<unsigned long>(global_state.flags.raw));
}


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
        PowerOutput::toggle(TAG);
    }
}



extern "C" void app_main(void){
    ESP_ERROR_CHECK(Blackbox::init());
    global_state.flags.bits.blackbox_enabled = Blackbox::is_enabled();
    HXC::NVS_Base::setup();
    ESP_ERROR_CHECK(BlackboxService::init());

    append_boot_line("boot: stage=hardware_config_init");
    esp_err_t hardware_ret = hardware_config_init();
    if (hardware_ret != ESP_OK) {
        append_boot_line("boot: hardware_config_init_failed err=%s", esp_err_to_name(hardware_ret));
        ESP_ERROR_CHECK(hardware_ret);
    }

    const esp_app_desc_t* app_desc = esp_app_get_description();
    BlackboxService::append_event("system: boot_start fw=%s build=%s_%s hw_version=%u",
                                  app_desc->version,
                                  app_desc->date,
                                  app_desc->time,
                                  static_cast<unsigned>(get_hardware_version()));
    append_early_boot_diagnostics();

    append_boot_line("boot: stage=temperature_init");
    ESP_ERROR_CHECK(Chip_Temperature_Sensor.init());
    ESP_ERROR_CHECK(Board_Temperature_sensor.init(get_hardware_config().temperature_channel));
    append_boot_line("boot: stage=state_timer_screen_start");
    TimerHandle_t xMyTimer = xTimerCreate("update_main_state", pdMS_TO_TICKS(5), pdTRUE, NULL, update_main_state); xTimerStart( xMyTimer, 0 );
    xTaskCreate(SCREEN::screen_task, "screen_task", 4096, NULL, 4, NULL);

    append_boot_line("boot: stage=lp_core_load");
    LP_Core_Load();
    append_boot_line("boot: stage=protect_init");
    ESP_ERROR_CHECK(protect_init());

    append_boot_line("boot: stage=output_init");
    ESP_ERROR_CHECK(PowerOutput::init(get_hardware_config().OUTPUT_CTRL));

    append_boot_line("boot: stage=button_init");
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

    append_boot_line("boot: stage=can_init");
    ESP_ERROR_CHECK(CanCallback::init());

    append_boot_line("boot: stage=shell_init");
    ESP_ERROR_CHECK(ShellCommand::init());

    append_boot_line("boot: stage=wifi_web_start");
    WebBackend::start_with_wifi_service();
    append_runtime_diagnostics();
    BlackboxService::append_event("system: app_ready");

    while (1){
        // Main loop only use for debug
        //ESP_LOGI("app_main", "ina226_run register: %d %d", *current_register_raw, *voltage_register_raw);

        vTaskDelay(1000/ portTICK_PERIOD_MS);
    }
    
}
