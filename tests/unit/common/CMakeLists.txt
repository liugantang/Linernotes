# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME common_TestSupport
    SOURCES
        tst_TestSupport.cpp
    LIBS
        Linernotes::TestSupport
)
