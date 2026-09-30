# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME butler_ItunesSearch
    SOURCES
        tst_ItunesSearch.cpp
    LIBS
        Linernotes::butler
        Linernotes::core
        Linernotes::TestSupport
        Linernotes::Deps::ICU
)



linernotes_add_test(
    NAME butler_DuplicateFinder
    SOURCES
        tst_DuplicateFinder.cpp
    LIBS
        Linernotes::butler
        Linernotes::audio
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
    NAME butler_TitleVersion
    SOURCES
        tst_TitleVersion.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Linernotes::Deps::ICU
)

linernotes_add_test(
    NAME butler_TranslationLlm
    SOURCES
        tst_TranslationLlm.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME butler_VersionSuffixLlm
    SOURCES
        tst_VersionSuffixLlm.cpp
    LIBS
        Linernotes::butler
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)



