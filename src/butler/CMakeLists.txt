# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

file(GLOB SCHEMA_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/schemas/*/*.json")

add_library(linernotes_butler STATIC
    ArtistSplit.cpp
    ArtistSplit.h
    ArtistSplitJobHandler.cpp
    ArtistSplitJobHandler.h
    ArtistSplitLlm.cpp
    ArtistSplitLlm.h
    ArtistSplitSource.cpp
    ArtistSplitSource.h
    ButlerLogging.cpp
    ButlerLogging.h
    CharFrequency.cpp
    CharFrequency.h
    EncodingScore.cpp
    EncodingScore.h
    Errors.h
    Mojibake.cpp
    Mojibake.h
    MojibakeAnalysis.cpp
    MojibakeAnalysis.h
    MojibakeJobHandler.cpp
    MojibakeJobHandler.h
    MojibakeLlm.cpp
    MojibakeLlm.h
    MojibakeSource.cpp
    MojibakeSource.h
)
add_library(Linernotes::butler ALIAS linernotes_butler)

qt6_extract_metatypes(linernotes_butler)

if(SCHEMA_FILES)
    qt_add_resources(linernotes_butler "schemas"
        PREFIX "/schemas"
        BASE "${PROJECT_SOURCE_DIR}/resources/schemas"
        FILES ${SCHEMA_FILES}
    )
endif()

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
        Qt6::Sql
    PRIVATE
        Linernotes::Deps::ICU
        Linernotes::Deps::Uchardet
)

linernotes_set_target_options(linernotes_butler)

