# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME butler_AlbumMatch
    SOURCES
        tst_AlbumMatch.cpp
    LIBS
        Linernotes::butler
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME butler_Mojibake
    SOURCES
        tst_Mojibake.cpp
    LIBS
        Linernotes::butler
        Linernotes::core
        Linernotes::TestSupport
        Linernotes::Deps::ICU
)

linernotes_add_test(
    NAME butler_MojibakeAnalysis
    SOURCES
        tst_MojibakeAnalysis.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Linernotes::Deps::ICU
)

linernotes_add_test(
    NAME butler_MojibakeLlm
    SOURCES
        tst_MojibakeLlm.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME butler_ArtistGroup
    SOURCES
        tst_ArtistGroup.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME butler_ArtistCreditLlm
    SOURCES
        tst_ArtistCreditLlm.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)


linernotes_add_test(
    NAME butler_ArtistMerge
    SOURCES
        tst_ArtistMerge.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME butler_ArtistMergeLlm
    SOURCES
        tst_ArtistMergeLlm.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME butler_ArtistName
    SOURCES
        tst_ArtistName.cpp
    LIBS
        Linernotes::butler
        Linernotes::core
        Linernotes::TestSupport
        Linernotes::Deps::ICU
)

linernotes_add_test(
    NAME butler_MusicBrainz
    SOURCES
        tst_MusicBrainz.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Qt6::Network
)



