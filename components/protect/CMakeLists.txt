idf_component_register(
                    SRCS "src/protect.cpp" 
                    PRIV_REQUIRES global_state freertos log
                    INCLUDE_DIRS "include")
