// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    property var artistId: 0
    signal albumSelected(var albumId)

    readonly property var info: (root.artistId > 0 && AppContext.actions)
        ? AppContext.actions.artistInfo(root.artistId) : ({})

    TrackListModel {
        id: artistTracksModel
        context: AppContext
        artistId: root.artistId
    }

    function playAll() {
        const ids = artistTracksModel.allTrackIds()
        if (ids.length > 0 && AppContext.actions) {
            AppContext.actions.playTracks(ids, 0)
        }
    }

    function playTrackAt(index) {
        const ids = artistTracksModel.allTrackIds()
        if (ids.length > 0 && AppContext.actions) {
            AppContext.actions.playTracks(ids, index)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Artist Header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium

            Rectangle {
                Layout.preferredWidth: Theme.artistAvatarSizeLarge
                Layout.preferredHeight: Theme.artistAvatarSizeLarge
                radius: Theme.artistAvatarSizeLarge / 2
                color: Theme.surfaceVariant
                clip: true

                Label {
                    anchors.centerIn: parent
                    visible: bigAvatarImg.status !== Image.Ready || !(root.info && root.info.coverHash)
                    text: (root.info && root.info.name && root.info.name.length > 0)
                        ? root.info.name.charAt(0).toUpperCase() : "?"
                    font.pixelSize: 36
                    font.bold: true
                    color: Theme.textSecondary
                }

                Image {
                    id: bigAvatarImg
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: (root.info && root.info.coverHash)
                        ? ("image://cover/" + encodeURIComponent(root.info.coverHash)) : ""
                    sourceSize: Qt.size(Theme.artistAvatarSizeLarge, Theme.artistAvatarSizeLarge)
                    visible: status === Image.Ready
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: (root.info && root.info.name) ? root.info.name : qsTr("Unknown Artist")
                    font.pixelSize: Theme.fontSizeTitle
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("%1 albums · %2 tracks")
                        .arg(root.info ? (root.info.albumCount || 0) : 0)
                        .arg(root.info ? (root.info.trackCount || 0) : 0)
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }

                RowLayout {
                    Button {
                        text: qsTr("Play All")
                        highlighted: true
                        onClicked: root.playAll()
                    }
                }
            }
        }

        TabBar {
            id: artistTabBar
            Layout.fillWidth: true

            TabButton {
                text: qsTr("Albums (%1)").arg(root.info ? (root.info.albumCount || 0) : 0)
            }

            TabButton {
                text: qsTr("All Tracks (%1)").arg(root.info ? (root.info.trackCount || 0) : 0)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: artistTabBar.currentIndex

            // Tab 0: Albums Grid
            AlbumGrid {
                artistId: root.artistId
                showToolbar: false
                onAlbumClicked: (selectedAlbumId) => {
                    root.albumSelected(selectedAlbumId)
                }
            }

            // Tab 1: All Tracks List
            ListView {
                id: artistTracksListView
                clip: true
                focus: true
                reuseItems: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                model: artistTracksModel

                Keys.onReturnPressed: (event) => {
                    if (currentIndex >= 0 && currentIndex < artistTracksModel.count) {
                        root.playTrackAt(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: (event) => {
                    if (currentIndex >= 0 && currentIndex < artistTracksModel.count) {
                        root.playTrackAt(currentIndex)
                        event.accepted = true
                    }
                }

                delegate: Rectangle {
                    id: trackRowDelegate
                    required property int index
                    required property var trackId
                    required property string title
                    required property string album
                    required property string durationText

                    width: ListView.view.width
                    height: Theme.trackRowHeight
                    radius: Theme.coverBorderRadius
                    color: {
                        if (artistTracksListView.currentIndex === trackRowDelegate.index && artistTracksListView.activeFocus) {
                            return Theme.itemSelected
                        }
                        if (rowMouse.containsMouse) {
                            return Theme.itemHover
                        }
                        return "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        Label {
                            text: trackRowDelegate.index + 1
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            Layout.preferredWidth: 32
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: trackRowDelegate.title || qsTr("Unknown Title")
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: trackRowDelegate.album
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: trackRowDelegate.durationText
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            artistTracksListView.currentIndex = trackRowDelegate.index
                        }
                        onDoubleClicked: {
                            root.playTrackAt(trackRowDelegate.index)
                        }
                    }
                }
            }
        }
    }
}
