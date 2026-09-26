# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_player STATIC
    GainRamp.cpp
    GainRamp.h
    MpvHandle.cpp
    MpvHandle.h
    PlaybackSnapshot.cpp
    PlaybackSnapshot.h
    Player.cpp
    Player.h
    PlayerLogging.cpp
    PlayerLogging.h
    PlayMode.h
    PlayOrder.cpp
    PlayOrder.h
    PlayQueue.cpp
    PlayQueue.h
    VoiceChannel.cpp
    VoiceChannel.h
)
add_library(Linernotes::player ALIAS linernotes_player)

target_include_directories(linernotes_player
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_player
    PUBLIC
        Linernotes::core
        Qt6::Core
    PRIVATE
        Linernotes::Deps::Mpv
)

linernotes_set_target_options(linernotes_player)
