# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

add_library(aimusic_testsupport STATIC
    TestSupport.cpp
    TestSupport.h
)
add_library(AiMusic::TestSupport ALIAS aimusic_testsupport)

target_include_directories(aimusic_testsupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(aimusic_testsupport
    PUBLIC
        Qt6::Core
)

aimusic_set_target_options(aimusic_testsupport)
