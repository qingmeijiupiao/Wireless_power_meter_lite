idf_component_register(
    SRCS "src/blackbox_service.cpp"
         "src/blackbox_snapshot.cpp"
         "src/blackbox_config.cpp"
         "src/blackbox_log_capture.cpp"
         "src/blackbox_worker.cpp"
    REQUIRES blackbox global_state HXC_NVS diagnostic_log esp_timer log freertos
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
)
