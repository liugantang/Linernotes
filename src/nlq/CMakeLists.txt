# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_nlq STATIC
    NlqLogging.cpp
    NlqLogging.h
    NlqQuery.cpp
    NlqQuery.h
    QueryRunner.cpp
    QueryRunner.h
    Errors.h
)
add_library(Linernotes::nlq ALIAS linernotes_nlq)

qt6_extract_metatypes(linernotes_nlq)

target_include_directories(linernotes_nlq
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_nlq
    PUBLIC
        Linernotes::core
        Linernotes::library
        Qt6::Core
        Qt6::Sql
)

linernotes_set_target_options(linernotes_nlq)
