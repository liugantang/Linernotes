# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME ai_SseParser
    SOURCES
        tst_SseParser.cpp
    LIBS
        Linernotes::ai
)

linernotes_add_test(
    NAME ai_LlmClient
    SOURCES
        tst_LlmClient.cpp
    LIBS
        Linernotes::ai
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME ai_LlmFixtures
    SOURCES
        tst_LlmFixtures.cpp
    LIBS
        Linernotes::ai
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME ai_JsonSchema
    SOURCES
        tst_JsonSchema.cpp
    LIBS
        Linernotes::ai
)

linernotes_add_test(
    NAME ai_StructuredOutput
    SOURCES
        tst_StructuredOutput.cpp
    LIBS
        Linernotes::ai
)

linernotes_add_test(
    NAME ai_CapabilityProbe
    SOURCES
        tst_CapabilityProbe.cpp
    LIBS
        Linernotes::ai
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME ai_AiConfig
    SOURCES
        tst_AiConfig.cpp
    LIBS
        Linernotes::ai
        Linernotes::core
)

linernotes_add_test(
    NAME ai_SecretStore
    SOURCES
        tst_SecretStore.cpp
    LIBS
        Linernotes::ai
        Linernotes::core
)

linernotes_add_test(
    NAME ai_LlmCache
    SOURCES
        tst_LlmCache.cpp
    LIBS
        Linernotes::ai
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ai_LlmService
    SOURCES
        tst_LlmService.cpp
    LIBS
        Linernotes::ai
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ai_UsageStore
    SOURCES
        tst_UsageStore.cpp
    LIBS
        Linernotes::ai
        Linernotes::library
        Linernotes::core
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ai_RetryPolicy
    SOURCES
        tst_RetryPolicy.cpp
    LIBS
        Linernotes::ai
        Linernotes::core
)

linernotes_add_test(
    NAME ai_RequestScheduler
    SOURCES
        tst_RequestScheduler.cpp
    LIBS
        Linernotes::ai
        Linernotes::core
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME ai_PromptLibrary
    SOURCES
        tst_PromptLibrary.cpp
    LIBS
        Linernotes::ai
        Linernotes::core
)

linernotes_add_test(
    NAME ai_PrivacyGuard
    SOURCES
        tst_PrivacyGuard.cpp
    LIBS
        Linernotes::ai
        Linernotes::core
)



