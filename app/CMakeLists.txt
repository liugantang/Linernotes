# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

qt_add_executable(linernotes
    main.cpp
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/Main.qml
    PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/Theme.qml
    PROPERTIES
        QT_RESOURCE_ALIAS Theme.qml
        QT_QML_SINGLETON_TYPE TRUE
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/NavigationSidebar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS NavigationSidebar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/PlayerBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS PlayerBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SidePanel.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SidePanel.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/AlbumGrid.qml
    PROPERTIES
        QT_RESOURCE_ALIAS AlbumGrid.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/AlbumDetail.qml
    PROPERTIES
        QT_RESOURCE_ALIAS AlbumDetail.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/ArtistDetail.qml
    PROPERTIES
        QT_RESOURCE_ALIAS ArtistDetail.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/TracksPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/TracksPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/AlbumsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/AlbumsPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/ArtistsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/ArtistsPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/PlaylistsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/PlaylistsPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/AiPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/AiPage.qml
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppButton.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/IconButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/IconButton.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppComboBox.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppComboBox.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppTabBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppTabBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppTabButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppTabButton.qml
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/play.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/play.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/pause.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/pause.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/skip-back.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/skip-back.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/skip-forward.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/skip-forward.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/shuffle.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/shuffle.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-close.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/panel-right-close.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-open.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/panel-right-open.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-left.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-left.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-up-narrow-wide.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-up-narrow-wide.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-down-wide-narrow.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-down-wide-narrow.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/disc-3.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/disc-3.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/mic-vocal.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/mic-vocal.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/list-music.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/list-music.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/sparkles.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/sparkles.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/library.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/library.svg
)

qt_add_qml_module(linernotes
    URI Linernotes
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        ${CMAKE_SOURCE_DIR}/qml/controls/AppButton.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/IconButton.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppComboBox.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppTabBar.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppTabButton.qml
        ${CMAKE_SOURCE_DIR}/qml/Main.qml
        ${CMAKE_SOURCE_DIR}/qml/Theme.qml
        ${CMAKE_SOURCE_DIR}/qml/NavigationSidebar.qml
        ${CMAKE_SOURCE_DIR}/qml/PlayerBar.qml
        ${CMAKE_SOURCE_DIR}/qml/SidePanel.qml
        ${CMAKE_SOURCE_DIR}/qml/AlbumGrid.qml
        ${CMAKE_SOURCE_DIR}/qml/AlbumDetail.qml
        ${CMAKE_SOURCE_DIR}/qml/ArtistDetail.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/TracksPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/AlbumsPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/ArtistsPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/PlaylistsPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/AiPage.qml
    RESOURCES
        ${CMAKE_SOURCE_DIR}/resources/icons/play.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/pause.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/skip-back.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/skip-forward.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/shuffle.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-close.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-open.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-left.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-up-narrow-wide.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-down-wide-narrow.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/disc-3.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/mic-vocal.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/list-music.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/sparkles.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/library.svg
    SOURCES
        AppInfo.cpp
        AppInfo.h
        QmlTypes.cpp
        QmlTypes.h
)

target_link_libraries(linernotes
    PRIVATE
        Linernotes::core
        Linernotes::library
        Linernotes::player
        Linernotes::ui
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

linernotes_set_target_options(linernotes)
