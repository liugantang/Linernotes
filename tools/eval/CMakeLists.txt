# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes-eval
    ArtistCreditEval.cpp
    ArtistCreditEval.h
    ArtistMergeEval.cpp
    ArtistMergeEval.h
    EvalHarness.cpp
    EvalHarness.h
    NlqAskEval.cpp
    NlqAskEval.h
    NlqEval.cpp
    NlqEval.h
    main.cpp
)

target_compile_definitions(linernotes-eval
    PRIVATE
        DEFAULT_ARTIST_MERGE_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/artist_merge.json"
        DEFAULT_ARTIST_CREDIT_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/artist_credit.json"
        DEFAULT_NLQ_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/nlq.json"
)

target_link_libraries(linernotes-eval
    PRIVATE
        Linernotes::butler
        Linernotes::nlq
        Linernotes::ai
        Linernotes::library
        Linernotes::core
        Qt6::Core
        Qt6::Network
        Qt6::Sql
)

linernotes_set_target_options(linernotes-eval)
