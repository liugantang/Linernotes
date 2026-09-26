# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_subdirectory(common)

if(LINERNOTES_BUILD_TESTS)
    add_subdirectory(unit)
    add_subdirectory(integration)
endif()

if(LINERNOTES_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
