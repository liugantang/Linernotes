# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_ai STATIC
    AiConfig.cpp
    AiConfig.h
    AiEnumNames.cpp
    AiEnumNames.h
    AiEnums.h
    AiLogging.cpp
    AiLogging.h
    CapabilityProbe.cpp
    CapabilityProbe.h
    ChatTypes.cpp
    ChatTypes.h
    Errors.h
    JsonSchema.cpp
    JsonSchema.h
    KeychainSecretStore.cpp
    KeychainSecretStore.h
    LlmCache.cpp
    LlmCache.h
    LlmClient.cpp
    LlmClient.h
    LlmReply.cpp
    LlmReply.h
    LlmService.cpp
    LlmService.h
    RequestScheduler.cpp
    RequestScheduler.h
    RetryPolicy.cpp
    RetryPolicy.h
    SecretStore.cpp
    SecretStore.h
    SseParser.cpp
    SseParser.h
    StructuredOutput.cpp
    StructuredOutput.h
    UsageStore.cpp
    UsageStore.h
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
        Linernotes::library
        Qt6::Core
        Qt6::Network
    PRIVATE
        Linernotes::Deps::JsonSchema
        Linernotes::Deps::Keychain
)

linernotes_set_target_options(linernotes_ai)
