idf_component_register(
                    SRCS "src/protect.cpp" 
                    PRIV_REQUIRES global_state freertos log blackbox_service
                    INCLUDE_DIRS "include")
