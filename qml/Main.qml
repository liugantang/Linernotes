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

    Shortcut {
        sequence: StandardKey.Preferences
        onActivated: sidebar.currentPage = NavigationSidebar.Settings
    }

    Shortcut {
        sequence: "Ctrl+,"
        onActivated: sidebar.currentPage = NavigationSidebar.Settings
    }

    function openAlbum(albumId) {
        sidebar.currentPage = NavigationSidebar.Albums
        albumsPage.openAlbum(albumId)
    }

    function openArtist(artistId) {
        sidebar.currentPage = NavigationSidebar.Artists
        artistsPage.openArtist(artistId)
    }

    function startTrackDrag(ids) {
        if (!ids || ids.length === 0) return
        dragGhostContainer.currentTrackIds = ids
        ghostRect.grabToImage((result) => {
            dragSourceItem.Drag.imageSource = result.url
            dragSourceItem.Drag.mimeData = {
                "application/x-linernotes-track-ids": ids.join(",")
            }
            dragSourceItem.Drag.active = true
        })
    }

    Item {
        id: dragGhostContainer
        x: -9999
        y: -9999
        width: ghostRect.width
        height: ghostRect.height
        visible: true
        z: -100

        property var currentTrackIds: []

        Rectangle {
            id: ghostRect
            width: ghostLabel.implicitWidth + Theme.spacingMedium * 2
            height: Theme.controlHeight
            radius: Theme.radiusMedium
            color: Theme.surface
            border.color: Theme.accent
            border.width: 1

            Label {
                id: ghostLabel
                anchors.centerIn: parent
                text: qsTr("%n track(s)", "", dragGhostContainer.currentTrackIds ? dragGhostContainer.currentTrackIds.length : 0)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.text
            }
        }

        Item {
            id: dragSourceItem
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
            Drag.mimeData: {
                "application/x-linernotes-track-ids": dragGhostContainer.currentTrackIds.join(",")
            }
            Drag.hotSpot.x: ghostRect.width / 2
            Drag.hotSpot.y: ghostRect.height / 2
        }
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

                    SearchBox {
                        id: searchBox
                        Layout.preferredWidth: 280
                        onFocusTrackListRequested: {
                            if (searchResults.visible) {
                                searchResults.focusTrackList()
                            }
                        }
                    }

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

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    StackLayout {
                        id: contentStack
                        anchors.fill: parent
                        visible: !searchResults.visible
                        currentIndex: sidebar.currentPage

                        TracksPage {}
                        AlbumsPage { id: albumsPage }
                        ArtistsPage { id: artistsPage }
                        PlaylistsPage {}
                        AiPage {}
                        SettingsPage {}
                    }

                    SearchResults {
                        id: searchResults
                        anchors.fill: parent
                        visible: AppContext.search && AppContext.search.active
                        onOpenAlbumRequested: (albumId) => window.openAlbum(albumId)
                        onOpenArtistRequested: (artistId) => window.openArtist(artistId)
                        onFocusSearchBoxRequested: searchBox.focusInput()
                    }
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
