idf_component_register(SRCS "blackbox.cpp"
                    PRIV_REQUIRES esp_partition spi_flash freertos log esp_timer global_state
                    INCLUDE_DIRS "include")
