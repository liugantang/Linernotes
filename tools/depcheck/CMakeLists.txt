# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes_depcheck
    depcheck.cpp
)

target_link_libraries(linernotes_depcheck
    PRIVATE
        Qt6::Core
        Linernotes::Deps::Mpv
        Linernotes::Deps::TagLib
        Linernotes::Deps::Uchardet
        Linernotes::Deps::ICU
        Linernotes::Deps::Chromaprint
        Linernotes::Deps::FFmpeg
        Linernotes::Deps::Ebur128
        Linernotes::Deps::Keychain
)

linernotes_set_target_options(linernotes_depcheck)
