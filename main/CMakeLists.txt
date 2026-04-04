idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES
        spi_flash
        blackbox
        hal
        cpp_gpio_driver
        esp_timer
        st7735_driver
        Temperature
        Fonts
        HXC_TWAI
        global_state
        protect
        ulp
        soc
        log
        freertos
        littlefs
        ui_resources
    INCLUDE_DIRS ""
)

# LP核程序配置
set(ulp_app_name ulp_main)
set(ulp_sources "ulp/ulp_main.cpp" "ulp/ina226.hpp")
set(ulp_exp_dep_srcs "main.cpp")

ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}" REQUIRES ulp_lp_core)

# 构建脚本目录
set(SCRIPT_DIR "${CMAKE_SOURCE_DIR}/scripts")

# 构建前处理
add_custom_target(run_pre_build ALL
    COMMAND python ${SCRIPT_DIR}/pre_build.py
    COMMENT "Running pre-build Python script..."
    VERBATIM
)
add_dependencies(${COMPONENT_LIB} run_pre_build)

# 构建后处理
add_custom_target(run_post_build ALL
    COMMAND python ${SCRIPT_DIR}/post_build.py
    COMMENT "Running post-build Python script..."
    VERBATIM
)
add_dependencies(run_post_build app bootloader)