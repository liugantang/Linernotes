# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 AiMusic contributors

qt_add_executable(aimusic
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

qt_add_qml_module(aimusic
    URI AiMusic
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        ${CMAKE_SOURCE_DIR}/qml/Main.qml
        ${CMAKE_SOURCE_DIR}/qml/Theme.qml
    SOURCES
        AppInfo.h
        AppInfo.cpp
)

target_link_libraries(aimusic
    PRIVATE
        AiMusic::core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
)

aimusic_set_target_options(aimusic)
