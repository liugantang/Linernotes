# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME audio_AudioDecoder
    SOURCES
        tst_AudioDecoder.cpp
    LIBS
        Linernotes::audio
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME audio_Fingerprint
    SOURCES
        tst_Fingerprint.cpp
    LIBS
        Linernotes::audio
        Linernotes::core
        Linernotes::TestSupport
)
