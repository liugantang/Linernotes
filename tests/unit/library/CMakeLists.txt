# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME library_CoverStore
    SOURCES
        tst_CoverStore.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Concurrent
)

linernotes_add_test(
    NAME library_Database
    SOURCES
        tst_Database.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_Migrator
    SOURCES
        tst_Migrator.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_Schema
    SOURCES
        tst_Schema.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_TagReader
    SOURCES
        tst_TagReader.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Concurrent
)

linernotes_add_test(
    NAME library_FileFingerprint
    SOURCES
        tst_FileFingerprint.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_LibraryRoots
    SOURCES
        tst_LibraryRoots.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_LibraryQuery
    SOURCES
        tst_LibraryQuery.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME library_DirectoryWalker
    SOURCES
        tst_DirectoryWalker.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_EntityLinker
    SOURCES
        tst_EntityLinker.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_SearchText
    SOURCES
        tst_SearchText.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME library_SearchIndex
    SOURCES
        tst_SearchIndex.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME library_FsType
    SOURCES
        tst_FsType.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_PlaylistStore
    SOURCES
        tst_PlaylistStore.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_LibrarySearch
    SOURCES
        tst_LibrarySearch.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME library_SmartRule
    SOURCES
        tst_SmartRule.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_MarkStore
    SOURCES
        tst_MarkStore.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_OverrideStore
    SOURCES
        tst_OverrideStore.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME library_PlayCountRule
    SOURCES
        tst_PlayCountRule.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_PlayEventStore
    SOURCES
        tst_PlayEventStore.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_PlayStats
    SOURCES
        tst_PlayStats.cpp
    LIBS
        Linernotes::library
)

linernotes_add_test(
    NAME library_DatabaseBackup
    SOURCES
        tst_DatabaseBackup.cpp
    LIBS
        Linernotes::library
)






