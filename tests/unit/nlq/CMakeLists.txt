# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME nlq_NlqQuery
    SOURCES
        tst_NlqQuery.cpp
    LIBS
        Linernotes::nlq
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME nlq_LibrarySummary
    SOURCES
        tst_LibrarySummary.cpp
    LIBS
        Linernotes::nlq
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME nlq_Interpreter
    SOURCES
        tst_Interpreter.cpp
    LIBS
        Linernotes::nlq
        Linernotes::ai
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME nlq_EntityResolver
    SOURCES
        tst_EntityResolver.cpp
    LIBS
        Linernotes::nlq
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
)
