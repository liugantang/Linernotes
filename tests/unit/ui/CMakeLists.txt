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
    NAME ui_NowPlaying
    SOURCES
        tst_NowPlaying.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_ErrorText
    SOURCES
        tst_ErrorText.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::core
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

linernotes_add_test(
    NAME ui_QueueModel
    SOURCES
        tst_QueueModel.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_PlaylistController
    SOURCES
        tst_PlaylistController.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_MarksController
    SOURCES
        tst_MarksController.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_NlqController
    SOURCES
        tst_NlqController.cpp
    LIBS
        Linernotes::ui
        Linernotes::nlq
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_CoverAccent
    SOURCES
        tst_CoverAccent.cpp
    LIBS
        Linernotes::ui
        Qt6::Gui
)

linernotes_add_test(
    NAME ui_AiSettingsController
    SOURCES
        tst_AiSettingsController.cpp
    LIBS
        Linernotes::ui
        Linernotes::ai
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Network
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_SettingsController
    SOURCES
        tst_SettingsController.cpp
    LIBS
        Linernotes::ui
        Linernotes::core
        Linernotes::player
        Linernotes::TestSupport
)

linernotes_add_test(
    NAME ui_LibraryRootsModel
    SOURCES
        tst_LibraryRootsModel.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_TagEditorModel
    SOURCES
        tst_TagEditorModel.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_TrackNotifier
    SOURCES
        tst_TrackNotifier.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::player
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_CorrectionReview
    SOURCES
        tst_CorrectionReview.cpp
    LIBS
        Linernotes::ui
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
)

linernotes_add_test(
    NAME ui_CleanupController
    SOURCES
        tst_CleanupController.cpp
    LIBS
        Linernotes::ui
        Linernotes::ai
        Linernotes::butler
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Network
        Qt6::Sql
        Qt6::Concurrent
)

linernotes_add_test(
    NAME ui_DuplicateController
    SOURCES
        tst_DuplicateController.cpp
    LIBS
        Linernotes::ui
        Linernotes::butler
        Linernotes::library
        Linernotes::TestSupport
        Qt6::Sql
        Qt6::Concurrent
)

if(UNIX AND NOT APPLE)
    linernotes_add_test(
        NAME ui_Mpris
        SOURCES
            tst_Mpris.cpp
        LIBS
            Linernotes::ui
            Linernotes::library
            Linernotes::player
            Linernotes::TestSupport
            Qt6::Sql
            Qt6::DBus
    )
endif()

