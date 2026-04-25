idf_component_register(
                    SRCS "src/screen.cpp" 
                    PRIV_REQUIRES st7735_driver hardware global_state protect freertos log ui_resources Fonts
                    INCLUDE_DIRS "include")
