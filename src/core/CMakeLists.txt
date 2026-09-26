# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

configure_file(
    Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h
    @ONLY
)

add_library(linernotes_core STATIC
    CoreSettings.h
    Logging.cpp
    Logging.h
    Paths.cpp
    Paths.h
    Result.h
    Settings.cpp
    Settings.h
    Version.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h
)
add_library(Linernotes::core ALIAS linernotes_core)

target_include_directories(linernotes_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(linernotes_core
    PUBLIC
        Qt6::Core
)

linernotes_set_target_options(linernotes_core)
