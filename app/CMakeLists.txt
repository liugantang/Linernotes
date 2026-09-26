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

qt_add_qml_module(linernotes
    URI Linernotes
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
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
