idf_component_register(
    SRCS "src/shell_command.cpp"
    PRIV_REQUIRES shell esp_system esp_timer hardware st7735_driver esp_app_format can_callback current_calibration global_state
    INCLUDE_DIRS "include"
)
