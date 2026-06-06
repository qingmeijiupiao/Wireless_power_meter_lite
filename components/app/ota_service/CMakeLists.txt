idf_component_register(
    SRCS "src/ota_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        app_update
        blackbox_service
        esp_http_client
        mbedtls
        ota_manager
)
