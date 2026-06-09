idf_component_register(
                    SRCS "src/TMP235.cpp" "src/ESPChipTemperatureSensor.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES ADC diagnostic_log esp_driver_tsens esp_hal_ana_conv
                    )
