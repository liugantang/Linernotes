// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtCore
import Linernotes
import "../controls" as Controls

Item {
    id: root

    property alias selectedPlaylistId: playlistList.selectedPlaylistId

    readonly property var playlistModel: AppContext.playlists ? AppContext.playlists.model : null
    readonly property bool hasSelectedPlaylist: root.selectedPlaylistId > 0
        && playlistModel !== null && playlistModel.contains(root.selectedPlaylistId)
    readonly property string selectedPlaylistName: hasSelectedPlaylist
        ? playlistModel.nameOf(root.selectedPlaylistId) : ""
    readonly property bool isManualPlaylist: root.hasSelectedPlaylist
        && AppContext.playlists && AppContext.playlists.isManual(root.selectedPlaylistId)
    readonly property bool isPlaylistOrderAscending: trackTable.model.sortKey === TrackListModel.PlaylistOrder
        && trackTable.model.sortOrder === Qt.AscendingOrder

    Settings {
        id: pageSettings
        location: AppContext.uiStateUrl
        category: "playlistsPage"
        property int selectedPlaylistId: 0
    }

    Component.onCompleted: {
        if (pageSettings.selectedPlaylistId > 0) {
            root.selectedPlaylistId = pageSettings.selectedPlaylistId
        }
    }

    onSelectedPlaylistIdChanged: {
        pageSettings.selectedPlaylistId = root.selectedPlaylistId
        if (root.selectedPlaylistId > 0) {
            trackTable.model.sortKey = TrackListModel.PlaylistOrder
            trackTable.model.sortOrder = Qt.AscendingOrder
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PlaylistList {
            id: playlistList
            Layout.preferredWidth: 240
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.divider
        }

        // Right column: Selected Playlist Content
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: root.hasSelectedPlaylist
                
                // Header
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingTiny

                        Label {
                            text: root.selectedPlaylistName
                            font.pixelSize: Theme.fontSizeTitle
                            font.bold: true
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: qsTr("%n track(s)", "", trackTable.count)
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                        }
                    }

                    Controls.AppButton {
                        text: qsTr("Edit Rules")
                        icon.source: "../icons/sparkles.svg"
                        visible: root.hasSelectedPlaylist && !root.isManualPlaylist
                        onClicked: {
                            playlistList.openSmartRuleDialog(root.selectedPlaylistId, root.selectedPlaylistName)
                        }
                    }

                    Controls.AppButton {
                        text: qsTr("Playlist Order")
                        visible: root.isManualPlaylist && trackTable.model.sortKey !== TrackListModel.PlaylistOrder
                        onClicked: {
                            trackTable.model.sortKey = TrackListModel.PlaylistOrder
                            trackTable.model.sortOrder = Qt.AscendingOrder
                        }
                    }

                    Controls.AppButton {
                        text: qsTr("Play")
                        primary: true
                        icon.source: "../icons/play.svg"
                        enabled: trackTable.count > 0
                        onClicked: {
                            if (AppContext.actions && trackTable.count > 0) {
                                AppContext.actions.playTracks(trackTable.model.allTrackIds(), 0)
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.divider
                }

                TrackTable {
                    id: trackTable
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    playlistId: root.selectedPlaylistId
                    persistSort: false
                    reorderable: root.isManualPlaylist && root.isPlaylistOrderAscending
                    emptyText: qsTr("This playlist is empty")
                }
            }

            // Unselected placeholder
            Label {
                anchors.centerIn: parent
                visible: !root.hasSelectedPlaylist
                text: qsTr("Select a playlist")
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.textSecondary
            }
        }
    }
}
