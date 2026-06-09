idf_component_register(
                    SRCS "src/protect.cpp" 
                    PRIV_REQUIRES diagnostic_log global_state freertos log HXC_NVS
                    INCLUDE_DIRS "include")
