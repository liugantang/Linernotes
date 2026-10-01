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
