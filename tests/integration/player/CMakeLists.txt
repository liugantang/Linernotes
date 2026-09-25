# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME player_Ducking
    SOURCES
        tst_Ducking.cpp
    LIBS
        Linernotes::player
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        60
)

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

linernotes_add_test(
    NAME player_PlayerQueue
    SOURCES
        tst_PlayerQueue.cpp
    LIBS
        Linernotes::player
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        120
)

linernotes_add_test(
    NAME player_PlayerErrors
    SOURCES
        tst_PlayerErrors.cpp
    LIBS
        Linernotes::player
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        120
)

linernotes_add_test(
    NAME player_VoiceChannel
    SOURCES
        tst_VoiceChannel.cpp
    LIBS
        Linernotes::player
        Linernotes::TestSupport
    LABELS
        integration
    TIMEOUT
        60
)
