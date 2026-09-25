# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

function(linernotes_add_test)
    cmake_parse_arguments(
        TEST
        ""
        "NAME;TIMEOUT"
        "SOURCES;LIBS;LABELS"
        ${ARGN}
    )

    if(NOT TEST_NAME)
        message(FATAL_ERROR "linernotes_add_test: NAME is required")
    endif()

    if(NOT TEST_SOURCES)
        message(FATAL_ERROR "linernotes_add_test: SOURCES is required")
    endif()

    if(NOT DEFINED TEST_TIMEOUT)
        set(TEST_TIMEOUT 30)
    endif()

    if(NOT DEFINED TEST_LABELS)
        set(TEST_LABELS unit)
    endif()

    set(target_name "tst_${TEST_NAME}")

    add_executable(${target_name}
        ${TEST_SOURCES}
    )

    target_link_libraries(${target_name}
        PRIVATE
            Qt6::Test
            ${TEST_LIBS}
    )

    linernotes_set_target_options(${target_name})

    add_test(NAME ${target_name} COMMAND ${target_name})

    set_property(TEST ${target_name} PROPERTY LABELS ${TEST_LABELS})
    set_property(TEST ${target_name} PROPERTY TIMEOUT ${TEST_TIMEOUT})
    set_property(TEST ${target_name} PROPERTY ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen"
        "QT_LOGGING_RULES=*.debug=false"
        "LINERNOTES_TEST_FIXTURES=${CMAKE_SOURCE_DIR}/tests/fixtures"
    )
endfunction()
