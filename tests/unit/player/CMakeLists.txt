# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME player_PlayOrder
    SOURCES
        tst_PlayOrder.cpp
    LIBS
        Linernotes::player
)
