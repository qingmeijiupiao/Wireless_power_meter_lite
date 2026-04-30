idf_component_register(
    SRCS "src/power_output.cpp"
    PRIV_REQUIRES cpp_gpio_driver hardware global_state protect log freertos esp_timer
    INCLUDE_DIRS "include"
)
