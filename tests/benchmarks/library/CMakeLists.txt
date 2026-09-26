# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes-bench-library
    bench_Library.cpp
)

target_link_libraries(linernotes-bench-library
    PRIVATE
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Core
        Qt6::Sql
)

linernotes_set_target_options(linernotes-bench-library)
