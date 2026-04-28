idf_component_register(
    SRCS "src/shell_command.cpp"
    PRIV_REQUIRES shell esp_system esp_timer hardware st7735_driver esp_app_format
    INCLUDE_DIRS "include"
)
