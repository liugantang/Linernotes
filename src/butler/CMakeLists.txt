# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_butler STATIC
    ButlerLogging.cpp
    ButlerLogging.h
    CharFrequency.cpp
    CharFrequency.h
    EncodingScore.cpp
    EncodingScore.h
    Errors.h
    Mojibake.cpp
    Mojibake.h
)
add_library(Linernotes::butler ALIAS linernotes_butler)

qt6_extract_metatypes(linernotes_butler)

target_include_directories(linernotes_butler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_butler
    PUBLIC
        Linernotes::core
        Linernotes::library
        Linernotes::ai
        Qt6::Core
    PRIVATE
        Linernotes::Deps::ICU
        Linernotes::Deps::Uchardet
)

linernotes_set_target_options(linernotes_butler)
