# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_audio STATIC
    AudioDecoder.cpp
    AudioDecoder.h
    AudioEmbedder.cpp
    AudioEmbedder.h
    AudioLogging.cpp
    AudioLogging.h
    EmbeddingIndex.cpp
    EmbeddingIndex.h
    Errors.h
    Fingerprint.cpp
    Fingerprint.h
    TrackEmbedding.cpp
    TrackEmbedding.h
)
add_library(Linernotes::audio ALIAS linernotes_audio)

qt6_extract_metatypes(linernotes_audio)

target_include_directories(linernotes_audio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_audio
    PUBLIC
        Linernotes::core
        Qt6::Core
    PRIVATE
        Linernotes::Deps::Chromaprint
        Linernotes::Deps::FFmpeg
        Linernotes::Deps::OnnxRuntime
)

linernotes_set_target_options(linernotes_audio)
