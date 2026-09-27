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

