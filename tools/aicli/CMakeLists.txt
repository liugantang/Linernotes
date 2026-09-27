# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes-aicli
    AiCli.cpp
    AiCli.h
    main.cpp
)

target_link_libraries(linernotes-aicli
    PRIVATE
        Linernotes::ai
        Linernotes::library
        Linernotes::core
        Qt6::Core
        Qt6::Network
)

linernotes_set_target_options(linernotes-aicli)
