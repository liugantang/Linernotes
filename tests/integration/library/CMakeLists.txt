# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME library_AlbumCovers
    SOURCES
        tst_AlbumCovers.cpp
    LIBS
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Qt6::Sql
    LABELS
        integration
    TIMEOUT
        60
)

linernotes_add_test(
    NAME library_Scanner
    SOURCES
        tst_Scanner.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        120
)

linernotes_add_test(
    NAME library_LibraryWatcher
    SOURCES
        tst_LibraryWatcher.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        120
)


linernotes_add_test(
    NAME library_ScanCli
    SOURCES
        tst_ScanCli.cpp
    LIBS
        Linernotes::library
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        60
)

target_compile_definitions(tst_library_ScanCli
    PRIVATE
        SCANCLI_PATH="$<TARGET_FILE:linernotes-scan>"
    )
add_dependencies(tst_library_ScanCli linernotes-scan)

linernotes_add_test(
    NAME library_Writeback
    SOURCES
        tst_Writeback.cpp
    LIBS
        Linernotes::library
        Linernotes::butler
        Linernotes::core
        Linernotes::ai
        Linernotes::TestSupport
        Qt6::Sql
    LABELS
        integration
    TIMEOUT
        60
)
