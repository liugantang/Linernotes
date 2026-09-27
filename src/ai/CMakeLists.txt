# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_ai STATIC
    AiLogging.cpp
    AiLogging.h
    ChatTypes.cpp
    ChatTypes.h
    Errors.h
    LlmClient.cpp
    LlmClient.h
    LlmReply.cpp
    LlmReply.h
    SseParser.cpp
    SseParser.h
)
add_library(Linernotes::ai ALIAS linernotes_ai)

qt6_extract_metatypes(linernotes_ai)

target_include_directories(linernotes_ai
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_ai
    PUBLIC
        Linernotes::core
        Qt6::Core
        Qt6::Network
)

linernotes_set_target_options(linernotes_ai)
