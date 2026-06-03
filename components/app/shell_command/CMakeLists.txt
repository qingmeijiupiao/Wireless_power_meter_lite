idf_component_register(
    SRCS "src/shell_command.cpp"
    PRIV_REQUIRES shell esp_system esp_timer hardware st7735_driver can_callback current_calibration global_state power_output wifi_service web_backend energy_meter blackbox blackbox_service screen
    INCLUDE_DIRS "include"
)
