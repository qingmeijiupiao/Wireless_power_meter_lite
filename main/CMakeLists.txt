idf_component_register(SRCS "app_main.cpp"
                       PRIV_REQUIRES spi_flash blackbox hal esp_adc Interp cpp_gpio_driver
                       REQUIRES ulp 
                       INCLUDE_DIRS "")
                       
# LP核程序配置
set(ulp_app_name ulp_main)                              # 二进制文件名，将生成 ulp_main.h 等
set(ulp_sources "ulp/ulp_main.c")                        # LP核源文件路径
set(ulp_exp_dep_srcs "main.c")                           # 主源文件（包含生成的ulp_main.h）

# 调用ulp_embed_binary
ulp_embed_binary(${ulp_app_name} "${ulp_sources}" "${ulp_exp_dep_srcs}" REQUIRES ulp_lp_core)

# 脚本目录
set(SCRIPT_DIR "${CMAKE_SOURCE_DIR}/scripts")

# 构建前处理：在每次构建时执行
add_custom_target(run_pre_build ALL
    COMMAND python ${SCRIPT_DIR}/pre_build.py
    COMMENT "Running pre-build Python script..."
    VERBATIM
)

# 确保 pre_build 在 main 组件构建之前执行
add_dependencies(${COMPONENT_LIB} run_pre_build)

# 构建后处理：在 app 构建完成后执行
add_custom_target(run_post_build ALL
    COMMAND python ${SCRIPT_DIR}/post_build.py
    COMMENT "Running post-build Python script..."
    VERBATIM
)

# 确保 post_build 目标在 app 之后执行
add_dependencies(run_post_build app bootloader)
#add_dependencies(run_post_build bootloader)