# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

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

