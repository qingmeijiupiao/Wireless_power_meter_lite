idf_component_register(
    SRCS "src/energy_meter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES global_state esp_timer freertos
)
