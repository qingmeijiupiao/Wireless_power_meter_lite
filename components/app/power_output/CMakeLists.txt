idf_component_register(
    SRCS "src/power_output.cpp"
    REQUIRES esp_driver_gpio
    PRIV_REQUIRES cpp_gpio_driver diagnostic_log hardware global_state protect log freertos esp_timer
    INCLUDE_DIRS "include"
)
