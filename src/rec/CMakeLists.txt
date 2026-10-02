# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_rec STATIC
    CandidatePool.cpp
    CandidatePool.h
    Errors.h
    RecLogging.cpp
    RecLogging.h
    Recommender.cpp
    Recommender.h
    SimilarTracks.cpp
    SimilarTracks.h
    SoundIndex.cpp
    SoundIndex.h
)
add_library(Linernotes::rec ALIAS linernotes_rec)

qt6_extract_metatypes(linernotes_rec)

target_include_directories(linernotes_rec
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_rec
    PUBLIC
        Linernotes::audio
        Linernotes::core
        Linernotes::library
        Qt6::Core
        Qt6::Sql
)

linernotes_set_target_options(linernotes_rec)
