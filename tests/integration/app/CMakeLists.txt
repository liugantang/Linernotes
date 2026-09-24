# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

add_test(
    NAME integration_app_SmokeTest
    COMMAND $<TARGET_FILE:aimusic> --smoke-test
)

set_property(TEST integration_app_SmokeTest PROPERTY LABELS integration)
set_property(TEST integration_app_SmokeTest PROPERTY TIMEOUT 30)
set_property(TEST integration_app_SmokeTest PROPERTY ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen"
    "QT_LOGGING_RULES=*.debug=false"
    "AIMUSIC_HOME=${CMAKE_CURRENT_BINARY_DIR}/tmp_smoke_test"
)
