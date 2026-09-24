# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

aimusic_add_test(
    NAME core_Version
    SOURCES
        tst_Version.cpp
    LIBS
        AiMusic::core
)

aimusic_add_test(
    NAME core_Logging
    SOURCES
        tst_Logging.cpp
    LIBS
        AiMusic::core
)

aimusic_add_test(
    NAME core_Paths
    SOURCES
        tst_Paths.cpp
    LIBS
        AiMusic::core
)

aimusic_add_test(
    NAME core_Settings
    SOURCES
        tst_Settings.cpp
    LIBS
        AiMusic::core
)

