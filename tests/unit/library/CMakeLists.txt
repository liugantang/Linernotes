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


