cmake_minimum_required(VERSION 3.25)

# 禁止在源码目录内构建
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please use a separate build directory.")
endif()

project(AiMusic
    VERSION 0.1.0
    DESCRIPTION "AI music player"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 未指定构建类型时默认 Debug（仅单配置生成器）
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

include(cmake/Dependencies.cmake)
include(cmake/CompilerOptions.cmake)
qt_standard_project_setup()

option(AIMUSIC_BUILD_TOOLS "Build developer tools" ON)

add_subdirectory(src)
add_subdirectory(app)

if(AIMUSIC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
