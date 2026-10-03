# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME guide_LrcParser
    SOURCES
        tst_LrcParser.cpp
    LIBS
        Linernotes::guide
        Linernotes::core
        Linernotes::TestSupport
        Qt6::Core
)

linernotes_add_test(
    NAME guide_Lrclib
    SOURCES
        tst_Lrclib.cpp
    LIBS
        Linernotes::guide
        Linernotes::butler
        Linernotes::core
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Core
        Qt6::Network
)

linernotes_add_test(
    NAME guide_LyricsStore
    SOURCES
        tst_LyricsStore.cpp
    LIBS
        Linernotes::guide
        Linernotes::core
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Core
        Qt6::Sql
)

linernotes_add_test(
    NAME guide_LocalLyrics
    SOURCES
        tst_LocalLyrics.cpp
    LIBS
        Linernotes::guide
        Linernotes::core
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Core
        Qt6::Sql
)

linernotes_add_test(
    NAME guide_LyricsService
    SOURCES
        tst_LyricsService.cpp
    LIBS
        Linernotes::guide
        Linernotes::core
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Core
        Qt6::Network
        Qt6::Sql
)
