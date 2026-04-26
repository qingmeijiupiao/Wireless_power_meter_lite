idf_component_register(
    SRCS "src/shell_command.cpp"
    PRIV_REQUIRES shell esp_system esp_timer hardware
    INCLUDE_DIRS "include"
)
