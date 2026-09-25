# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_testsupport STATIC
    TestSupport.cpp
    TestSupport.h
)
add_library(Linernotes::TestSupport ALIAS linernotes_testsupport)

target_include_directories(linernotes_testsupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_testsupport
    PUBLIC
        Qt6::Core
)

linernotes_set_target_options(linernotes_testsupport)
