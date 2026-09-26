# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_ui STATIC
    AlbumGridModel.cpp
    AlbumGridModel.h
    AppContext.cpp
    AppContext.h
    ArtistListModel.cpp
    ArtistListModel.h
    CoverImageProvider.cpp
    CoverImageProvider.h
    Format.h
    LibraryActions.cpp
    LibraryActions.h
    NowPlaying.cpp
    NowPlaying.h
    PageCache.h
    PagedListModel.cpp
    PagedListModel.h
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
    TrackListModel.cpp
    TrackListModel.h
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
