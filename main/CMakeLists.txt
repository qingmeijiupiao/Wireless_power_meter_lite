idf_component_register(
    SRCS "app_main.cpp" 
         "ulp_loader/ulp_loader.cpp"
    INCLUDE_DIRS "." 
                 "ulp_loader"
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
        ui_resources
        Button
        HXC_NVS
        shell
        web_file
        wifi_manager
    INCLUDE_DIRS ""
)

# LP核程序配置
set(ulp_app_name ulp_main)
set(ulp_sources "ulp_app/ulp_main.cpp")
set(ulp_exp_dep_srcs "ulp_loader/ulp_loader.cpp")

ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}")

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