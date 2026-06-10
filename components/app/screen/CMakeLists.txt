idf_component_register(
                    SRCS "src/screen.cpp"
                         "src/ui_common.cpp"
                         "src/ui_manager.cpp"
                         "src/curve_history.cpp"
                         "src/pages.cpp"
                    REQUIRES Button
                    PRIV_REQUIRES st7735_driver hardware diagnostic_log global_state protect freertos log ui_resources Fonts HXC_NVS wifi_service wifi_manager espnow_link espnow_service power_output can_callback can_resistor energy_meter blackbox blackbox_service current_calibration ota_service
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include")
