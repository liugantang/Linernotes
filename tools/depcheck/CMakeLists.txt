# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

add_executable(aimusic_depcheck
    depcheck.cpp
)

target_link_libraries(aimusic_depcheck
    PRIVATE
        Qt6::Core
        AiMusic::Deps::Mpv
        AiMusic::Deps::TagLib
        AiMusic::Deps::Uchardet
        AiMusic::Deps::ICU
        AiMusic::Deps::Chromaprint
        AiMusic::Deps::FFmpeg
        AiMusic::Deps::Ebur128
        AiMusic::Deps::Keychain
)

aimusic_set_target_options(aimusic_depcheck)
