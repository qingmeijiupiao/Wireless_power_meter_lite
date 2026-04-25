idf_component_register(
                    SRCS "hardware.cpp" "src/hardware.cpp"
                    INCLUDE_DIRS "include" 
                    REQUIRES esp_adc esp_driver_gpio
                    )
