idf_component_register(
    SRCS "src/power_output.cpp"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES cpp_gpio_driver hardware global_state protect log freertos esp_timer blackbox_service
    INCLUDE_DIRS "include"
)
