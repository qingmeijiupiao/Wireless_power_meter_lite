idf_component_register(
    SRCS "src/web_backend.cpp"
    INCLUDE_DIRS "include"
    REQUIRES WebServer web_file wifi_service global_state power_output protect esp_timer esp_system esp_http_server st7735_driver can_callback current_calibration hardware esp_app_format
)
