idf_component_register(
    SRCS "src/wifi_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES wifi_manager HXC_NVS DNSServer WebServer esp_wifi global_state blackbox_service time_service
)
