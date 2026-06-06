idf_component_register(
    SRCS
        "src/espnow_service_business.cpp"
        "src/espnow_service_business_protocol.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES
        espnow_link
        power_output
        energy_meter
        global_state
        blackbox_service
    PRIV_REQUIRES freertos log esp_system esp_timer
)
