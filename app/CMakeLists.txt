# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

add_executable(aimusic
    main.cpp
)

target_link_libraries(aimusic
    PRIVATE
        AiMusic::core
)

aimusic_set_target_options(aimusic)
