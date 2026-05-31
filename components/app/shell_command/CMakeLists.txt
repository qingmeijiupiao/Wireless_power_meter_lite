idf_component_register(
    SRCS "src/shell_command.cpp"
    PRIV_REQUIRES shell esp_system esp_timer hardware st7735_driver esp_app_format can_callback current_calibration global_state power_output wifi_service web_backend energy_meter blackbox blackbox_service
    INCLUDE_DIRS "include"
)
