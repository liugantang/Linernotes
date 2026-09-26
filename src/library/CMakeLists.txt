# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

file(GLOB MIGRATION_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/migrations/*.sql")

add_library(linernotes_library STATIC
    Database.cpp
    Database.h
    DirectoryWalker.cpp
    DirectoryWalker.h
    EntityLinker.cpp
    EntityLinker.h
    Errors.h
    FileFingerprint.cpp
    FileFingerprint.h
    LibraryLogging.cpp
    LibraryLogging.h
    LibraryRoots.cpp
    LibraryRoots.h
    Migrator.cpp
    Migrator.h
    Scanner.cpp
    Scanner.h
    TagReader.cpp
    TagReader.h
)
add_library(Linernotes::library ALIAS linernotes_library)

if(MIGRATION_FILES)
    qt_add_resources(linernotes_library "migrations"
        PREFIX "/migrations"
        BASE "${PROJECT_SOURCE_DIR}/resources/migrations"
        FILES ${MIGRATION_FILES}
    )
endif()

target_include_directories(linernotes_library
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_library
    PUBLIC
        Linernotes::core
        Qt6::Core
        Qt6::Sql
    PRIVATE
        Linernotes::Deps::SQLite3
        Linernotes::Deps::TagLib
)

linernotes_set_target_options(linernotes_library)
