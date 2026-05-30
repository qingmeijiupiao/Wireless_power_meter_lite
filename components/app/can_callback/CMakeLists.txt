idf_component_register(
    SRCS "src/can_callback.cpp"
    REQUIRES protect HXC_TWAI HXC_NVS can_resistor
    PRIV_REQUIRES hardware global_state power_output
    INCLUDE_DIRS "include"
)
