# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME integration_MojibakeLibrary
    SOURCES
        tst_MojibakeLibrary.cpp
    LIBS
        Linernotes::butler
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
    NAME integration_ArtistCreditJob
    SOURCES
        tst_ArtistCreditJob.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::ai
        Linernotes::TestSupport
        Qt6::Network
        Qt6::Sql
    LABELS
        integration
    TIMEOUT
        60
)

linernotes_add_test(
    NAME integration_ArtistMergeJob
    SOURCES
        tst_ArtistMergeJob.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::ai
        Linernotes::TestSupport
        Qt6::Network
        Qt6::Sql
    LABELS
        integration
    TIMEOUT
        60
)
