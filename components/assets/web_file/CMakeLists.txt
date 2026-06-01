
set(WEB_GZIP_FILES
    "index.html.gz"
    "charts.html.gz"
    "control.html.gz"
    "status.html.gz"
    "logs.html.gz"
    "blackbox.html.gz"
    "firmware.html.gz"
    "provision.html.gz"
    "app.css.gz"
)
get_filename_component(WEB_GZIP_SCRIPT
    "${CMAKE_CURRENT_LIST_DIR}/../../../scripts/pre_build.py"
    ABSOLUTE
)

# Ensure gzip inputs exist during configuration. scripts/pre_build.py also
# regenerates them before every build through main/run_pre_build.
execute_process(
    COMMAND python "${WEB_GZIP_SCRIPT}"
    RESULT_VARIABLE WEB_GZIP_RESULT
)
if(NOT WEB_GZIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate gzip web assets")
endif()

idf_component_register(
            SRCS "src/web_file.cpp"
            INCLUDE_DIRS "include"
            EMBED_FILES ${WEB_GZIP_FILES}
)
add_dependencies(${COMPONENT_LIB} run_pre_build)
