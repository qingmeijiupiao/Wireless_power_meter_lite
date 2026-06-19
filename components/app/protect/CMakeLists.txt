idf_component_register(
    SRCS "src/protect.cpp"
         "src/protect_mos.cpp"
    PRIV_REQUIRES diagnostic_log global_state freertos log HXC_NVS
    PRIV_INCLUDE_DIRS "private_include"
    INCLUDE_DIRS "include"
)
