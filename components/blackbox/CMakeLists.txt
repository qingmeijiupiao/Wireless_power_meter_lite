idf_component_register(SRCS "blackbox.cpp"
                    REQUIRES esp_partition spi_flash
                    INCLUDE_DIRS "include")
