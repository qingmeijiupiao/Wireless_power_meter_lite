idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_spi esp_driver_gpio PWM Interp
    PRIV_REQUIRES esp_timer log esp_hw_support freertos
)