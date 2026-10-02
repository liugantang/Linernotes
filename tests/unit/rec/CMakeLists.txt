# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME rec_SimilarTracks
    SOURCES
        tst_SimilarTracks.cpp
    LIBS
        Linernotes::rec
        Linernotes::audio
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME rec_Recommender
    SOURCES
        tst_Recommender.cpp
    LIBS
        Linernotes::rec
        Linernotes::audio
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME rec_SessionSeeds
    SOURCES
        tst_SessionSeeds.cpp
    LIBS
        Linernotes::rec
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
)
