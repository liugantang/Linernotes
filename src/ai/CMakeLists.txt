# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

file(GLOB PROMPT_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/prompts/*/*.md")

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
    HttpError.cpp
    HttpError.h
    JobHandler.cpp
    JobHandler.h
    JobQueue.cpp
    JobQueue.h
    JsonSchema.cpp
    JsonSchema.h
    KeychainSecretStore.cpp
    KeychainSecretStore.h
    LlmCache.cpp
    LlmCache.h
    LlmClient.cpp
    LlmClient.h
    LlmDebugLog.cpp
    LlmDebugLog.h
    LlmReply.cpp
    LlmReply.h
    LlmService.cpp
    LlmService.h
    ModelList.cpp
    ModelList.h
    PrivacyGuard.cpp
    PrivacyGuard.h
    PromptLibrary.cpp
    PromptLibrary.h
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

if(PROMPT_FILES)
    qt_add_resources(linernotes_ai "prompts"
        PREFIX "/prompts"
        BASE "${PROJECT_SOURCE_DIR}/resources/prompts"
        FILES ${PROMPT_FILES}
    )
endif()

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
