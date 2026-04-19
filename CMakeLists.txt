# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(EXTRA_COMPONENT_DIRS 
    "components/app"
    "components/assets"
    "components/bsp"
    "components/middleware"
)

project(Wireless_power_meter_lite)

# 创建 LittleFS 分区镜像
littlefs_create_partition_image(storage flash_storage_data)
