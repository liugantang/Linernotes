# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

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

