# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_guide STATIC
    Errors.h
    GuideLogging.cpp
    GuideLogging.h
    LocalLyrics.cpp
    LocalLyrics.h
    Lrclib.cpp
    Lrclib.h
    Lyrics.cpp
    Lyrics.h
    LyricsService.cpp
    LyricsService.h
    LyricsStore.cpp
    LyricsStore.h
)
add_library(Linernotes::guide ALIAS linernotes_guide)

qt6_extract_metatypes(linernotes_guide)

target_include_directories(linernotes_guide
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_guide
    PUBLIC
        Linernotes::butler
        Linernotes::core
        Linernotes::library
        Qt6::Core
        Qt6::Network
        Qt6::Sql
)

linernotes_set_target_options(linernotes_guide)
