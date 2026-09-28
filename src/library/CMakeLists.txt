# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

file(GLOB MIGRATION_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/migrations/*.sql")

add_library(linernotes_library STATIC
    ArtistAliasCorrections.cpp
    ArtistAliasCorrections.h
    CoverStore.cpp
    CoverStore.h
    CorrectionStore.cpp
    CorrectionStore.h
    Database.cpp
    Database.h
    DatabaseBackup.cpp
    DatabaseBackup.h
    DirectoryWalker.cpp
    DirectoryWalker.h
    EntityLinker.cpp
    EntityLinker.h
    EnumNames.h
    Errors.h
    FileFingerprint.cpp
    FileFingerprint.h
    FsType.cpp
    FsType.h
    LibraryLogging.cpp
    LibraryLogging.h
    LibraryQuery.cpp
    LibraryQuery.h
    LibraryQuerySql.cpp
    LibraryQuerySql.h
    LibraryRoots.cpp
    LibraryRoots.h
    LibrarySearch.cpp
    LibrarySearch.h
    LibraryWatcher.cpp
    LibraryWatcher.h
    MarkStore.cpp
    MarkStore.h
    MetadataRefresh.cpp
    MetadataRefresh.h
    Migrator.cpp
    Migrator.h
    OverrideStore.cpp
    OverrideStore.h
    PlayCountRule.cpp
    PlayCountRule.h
    PlayEventStore.cpp
    PlayEventStore.h
    PlayStats.cpp
    PlayStats.h
    PlaylistStore.cpp
    PlaylistStore.h
    ScanDiff.cpp
    ScanDiff.h
    Scanner.cpp
    Scanner.h
    ScanPipeline.cpp
    ScanPipeline.h
    ScanWriter.cpp
    ScanWriter.h
    SearchIndex.cpp
    SearchIndex.h
    SearchText.cpp
    SearchText.h
    SmartRule.cpp
    LibraryEnums.h
    SmartRule.h
    SmartRuleSql.cpp
    SmartRuleSql.h
    TagReader.cpp
    TagReader.h
    TrackCorrections.cpp
    TrackCorrections.h
    TrackIssueStore.cpp
    TrackIssueStore.h
)
add_library(Linernotes::library ALIAS linernotes_library)

qt6_extract_metatypes(linernotes_library)

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
        # Qt6::Gui is needed for QImage, QImageReader in CoverStore
        Qt6::Gui
        Qt6::Sql
    PRIVATE
        Linernotes::Deps::ICU
        Linernotes::Deps::SQLite3
        Linernotes::Deps::TagLib
)

linernotes_set_target_options(linernotes_library)
