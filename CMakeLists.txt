# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)

set(EXTRA_COMPONENT_DIRS 
    "components/app"
    "components/assets"
    "components/bsp"
    "components/common"
    "components/middleware"
)

# 版本号 MAJOR.MINOR 由开发者修改，PATCH 区分本地/CD
set(VERSION_MAJOR 0 CACHE STRING "Major version")
set(VERSION_MINOR 1 CACHE STRING "Minor version")
set(VERSION_PATCH 99 CACHE STRING "Patch version (99=local, 0=CD)")

set(PROJECT_VER "v${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

# Run generated-asset preprocessing before dependent component targets.
add_custom_target(run_pre_build ALL
    COMMAND python "${CMAKE_SOURCE_DIR}/scripts/pre_build.py"
    COMMENT "Running pre-build Python script..."
    VERBATIM
)

project(Wireless_power_meter_lite)

# 将版本号注入所有目标的编译定义
idf_build_set_property(COMPILE_DEFINITIONS 
    "VERSION_MAJOR=${VERSION_MAJOR}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS 
    "VERSION_MINOR=${VERSION_MINOR}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS 
    "VERSION_PATCH=${VERSION_PATCH}" APPEND)

# 注入编译时间
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M:%S" UTC)
idf_build_set_property(COMPILE_DEFINITIONS 
    "BUILD_TIME=\"${BUILD_TIME}\"" APPEND)
