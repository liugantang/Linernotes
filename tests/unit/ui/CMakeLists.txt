# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

linernotes_add_test(
    NAME ui_AppContext
    SOURCES
        tst_AppContext.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_LibraryActions
    SOURCES
        tst_LibraryActions.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_PageCache
    SOURCES
        tst_PageCache.cpp
    LIBS
        Linernotes::ui
        Linernotes::core
)

linernotes_add_test(
    NAME ui_RowSelection
    SOURCES
        tst_RowSelection.cpp
    LIBS
        Linernotes::ui
)

linernotes_add_test(
    NAME ui_TrackListModel
    SOURCES
        tst_TrackListModel.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)
