# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_ui STATIC
    AlbumGridModel.cpp
    AlbumGridModel.h
    AppContext.cpp
    AppContext.h
    AppSettings.h
    ArtistListModel.cpp
    ArtistListModel.h
    CoverAccent.cpp
    CoverAccent.h
    CoverImageProvider.cpp
    CoverImageProvider.h
    Format.h
    LibraryActions.cpp
    LibraryActions.h
    LibraryRootsModel.cpp
    LibraryRootsModel.h
    MarksController.cpp
    MarksController.h
    NowPlaying.cpp
    NowPlaying.h
    PageCache.h
    PagedListModel.cpp
    PagedListModel.h
    PlayEventRecorder.cpp
    PlayEventRecorder.h
    PlaylistController.cpp
    PlaylistController.h
    PlaylistListModel.cpp
    PlaylistListModel.h
    QueueModel.cpp
    QueueModel.h
    RowSelection.cpp
    RowSelection.h
    SearchController.cpp
    SearchController.h
    SettingsController.cpp
    SettingsController.h
    TagEditorModel.cpp
    TagEditorModel.h
    TrackListModel.cpp
    TrackListModel.h
    Translations.cpp
    Translations.h
    UiLogging.cpp
    UiLogging.h
)
add_library(Linernotes::ui ALIAS linernotes_ui)

qt6_extract_metatypes(linernotes_ui)

target_include_directories(linernotes_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_ui
    PUBLIC
        Linernotes::core
        Linernotes::library
        Linernotes::player
        Qt6::Core
        Qt6::Gui
        Qt6::Quick # Required by CoverImageProvider (QQuickImageProvider)
)

linernotes_set_target_options(linernotes_ui)
