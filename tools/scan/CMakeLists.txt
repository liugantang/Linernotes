# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes-scan
    main.cpp
)

target_link_libraries(linernotes-scan
    PRIVATE
        Linernotes::library
        Linernotes::core
        Qt6::Core
        Qt6::Sql
)

linernotes_set_target_options(linernotes-scan)
