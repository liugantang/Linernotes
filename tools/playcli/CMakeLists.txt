# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_executable(linernotes-playcli
    main.cpp
    PlayCli.cpp
    PlayCli.h
)

target_link_libraries(linernotes-playcli
    PRIVATE
        Linernotes::player
        Linernotes::core
        Qt6::Core
)

linernotes_set_target_options(linernotes-playcli)
