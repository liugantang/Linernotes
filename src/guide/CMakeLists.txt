# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

file(GLOB SCHEMA_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/schemas/guide/*.json")

add_library(linernotes_guide STATIC
    Errors.h
    GuideEnums.h
    GuideLogging.cpp
    GuideLogging.h
    GuideUtils.h
    LineExplainer.cpp
    LineExplainer.h
    LocalLyrics.cpp
    LocalLyrics.h
    LrcParser.cpp
    LrcParser.h
    Lrclib.cpp
    Lrclib.h
    Lyrics.cpp
    Lyrics.h
    LyricsService.cpp
    LyricsService.h
    LyricsStore.cpp
    LyricsStore.h
    LyricsTranslation.cpp
    LyricsTranslation.h
    LyricsTranslationStore.cpp
    LyricsTranslationStore.h
    LyricsTranslator.cpp
    LyricsTranslator.h
    Wikidata.cpp
    Wikidata.h
    Wikipedia.cpp
    Wikipedia.h
)
add_library(Linernotes::guide ALIAS linernotes_guide)

qt6_extract_metatypes(linernotes_guide)

if(SCHEMA_FILES)
    qt_add_resources(linernotes_guide "schemas"
        PREFIX "/schemas"
        BASE "${PROJECT_SOURCE_DIR}/resources/schemas"
        FILES ${SCHEMA_FILES}
    )
endif()

target_include_directories(linernotes_guide
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_guide
    PUBLIC
        Linernotes::ai
        Linernotes::butler
        Linernotes::core
        Linernotes::library
        Qt6::Core
        Qt6::Network
        Qt6::Sql
)

linernotes_set_target_options(linernotes_guide)
