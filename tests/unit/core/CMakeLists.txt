# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME core_Version
    SOURCES
        tst_Version.cpp
    LIBS
        Linernotes::core
)

linernotes_add_test(
    NAME core_Logging
    SOURCES
        tst_Logging.cpp
    LIBS
        Linernotes::core
)

linernotes_add_test(
    NAME core_Paths
    SOURCES
        tst_Paths.cpp
    LIBS
        Linernotes::core
)

linernotes_add_test(
    NAME core_Settings
    SOURCES
        tst_Settings.cpp
    LIBS
        Linernotes::core
)

