idf_component_register(
    SRCS "src/can_callback.cpp"
    PRIV_REQUIRES HXC_TWAI HXC_NVS hardware cpp_gpio_driver esp_log
    INCLUDE_DIRS "include"
)
