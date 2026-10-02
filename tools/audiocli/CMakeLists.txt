# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes-audio
    main.cpp
)

target_link_libraries(linernotes-audio
    PRIVATE
        Linernotes::audio
        Linernotes::library
        Linernotes::core
        Qt6::Core
        Qt6::Sql
)

linernotes_set_target_options(linernotes-audio)
