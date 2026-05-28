idf_component_register(
    SRCS "src/web_backend.cpp"
    INCLUDE_DIRS "include"
    REQUIRES WebServer web_file wifi_service global_state power_output protect esp_timer esp_http_server
)
