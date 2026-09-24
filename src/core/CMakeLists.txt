# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

configure_file(
    Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h
    @ONLY
)

add_library(aimusic_core STATIC
    CoreSettings.h
    Logging.cpp
    Logging.h
    Paths.cpp
    Paths.h
    Settings.cpp
    Settings.h
    Version.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h
)
add_library(AiMusic::core ALIAS aimusic_core)

target_include_directories(aimusic_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(aimusic_core
    PUBLIC
        Qt6::Core
)

aimusic_set_target_options(aimusic_core)
