idf_component_register(
    SRCS "src/wifi_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES wifi_manager HXC_NVS DNSServer WebServer diagnostic_log esp_wifi espnow_link espnow_service global_state time_service
)
