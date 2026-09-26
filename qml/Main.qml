// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "pages"
import "controls" as Controls

ApplicationWindow {
    id: window

    title: qsTr("Linernotes")
    width: Theme.windowDefaultWidth
    height: Theme.windowDefaultHeight
    minimumWidth: Theme.windowMinWidth
    minimumHeight: Theme.windowMinHeight
    visible: true
    color: Theme.background

    Shortcut {
        sequence: "Ctrl+Alt+P"
        onActivated: sidePanel.collapsed = !sidePanel.collapsed
    }

    function openAlbum(albumId) {
        sidebar.currentPage = "albums"
        albumsPage.openAlbum(albumId)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Main body: Sidebar + Content Area + SidePanel
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavigationSidebar {
                id: sidebar
                Layout.preferredWidth: Theme.sidebarWidth
                Layout.fillHeight: true
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.divider
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Top toolbar
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.topBarHeight
                    Layout.leftMargin: Theme.spacingMedium
                    Layout.rightMargin: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    Item {
                        Layout.fillWidth: true
                    }

                    Controls.IconButton {
                        icon.source: sidePanel.collapsed ? "qrc:/qt/qml/Linernotes/icons/panel-right-open.svg" : "qrc:/qt/qml/Linernotes/icons/panel-right-close.svg"
                        toolTip: sidePanel.collapsed ? qsTr("Show Panel (Ctrl+Alt+P)") : qsTr("Hide Panel (Ctrl+Alt+P)")
                        onClicked: sidePanel.collapsed = !sidePanel.collapsed
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.divider
                }

                // Startup error banner
                Rectangle {
                    visible: AppContext.startupError.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? (errorLabel.implicitHeight + Theme.spacingMedium * 2) : 0
                    color: Theme.errorBackground
                    border.color: Theme.errorBorder
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        Label {
                            id: errorLabel
                            text: qsTr("Startup Error: %1").arg(AppContext.startupError)
                            color: Theme.errorText
                            font.pixelSize: Theme.fontSizeNormal
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }

                StackLayout {
                    id: contentStack
                    currentIndex: {
                        switch (sidebar.currentPage) {
                        case "tracks": return 0
                        case "albums": return 1
                        case "artists": return 2
                        case "playlists": return 3
                        case "ai": return 4
                        default: return 0
                        }
                    }
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TracksPage {}
                    AlbumsPage { id: albumsPage }
                    ArtistsPage {}
                    PlaylistsPage {}
                    AiPage {}
                }
            }

            Rectangle {
                visible: !sidePanel.collapsed
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.divider
            }

            SidePanel {
                id: sidePanel
                visible: !collapsed
                Layout.preferredWidth: Theme.sidePanelWidth
                Layout.fillHeight: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        PlayerBar {
            id: playerBar
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.playerBarHeight
            onOpenAlbumRequested: function(albumId) {
                window.openAlbum(albumId)
            }
        }
    }
}
