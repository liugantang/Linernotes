# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

file(GLOB SCHEMA_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/schemas/nlq/*.json")

add_library(linernotes_nlq STATIC
    EntityResolver.cpp
    EntityResolver.h
    Errors.h
    Interpreter.cpp
    Interpreter.h
    LibrarySummary.cpp
    LibrarySummary.h
    NlqLogging.cpp
    NlqLogging.h
    NlqQuery.cpp
    NlqQuery.h
    OfflineParser.cpp
    OfflineParser.h
    QueryRunner.cpp
    QueryRunner.h
    Relaxation.cpp
    Relaxation.h
    SimilarQuery.cpp
    SimilarQuery.h
    VariantPruning.cpp
    VariantPruning.h
)
add_library(Linernotes::nlq ALIAS linernotes_nlq)

qt6_extract_metatypes(linernotes_nlq)

if(SCHEMA_FILES)
    qt_add_resources(linernotes_nlq "nlq_schemas"
        PREFIX "/schemas"
        BASE "${PROJECT_SOURCE_DIR}/resources/schemas"
        FILES ${SCHEMA_FILES}
    )
endif()

target_include_directories(linernotes_nlq
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_nlq
    PUBLIC
        Linernotes::core
        Linernotes::library
        Linernotes::rec
        Linernotes::ai
        Qt6::Core
        Qt6::Sql
)

linernotes_set_target_options(linernotes_nlq)
