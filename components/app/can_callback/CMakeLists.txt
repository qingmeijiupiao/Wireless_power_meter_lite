idf_component_register(
    SRCS "src/can_callback.cpp"
    REQUIRES cpp_gpio_driver protect HXC_TWAI HXC_NVS
    PRIV_REQUIRES hardware global_state power_output
    INCLUDE_DIRS "include"
)
