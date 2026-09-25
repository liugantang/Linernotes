# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME player_MpvHandle
    SOURCES
        tst_MpvHandle.cpp
    LIBS
        Linernotes::player
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        60
)

linernotes_add_test(
    NAME player_Player
    SOURCES
        tst_Player.cpp
    LIBS
        Linernotes::player
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        60
)

