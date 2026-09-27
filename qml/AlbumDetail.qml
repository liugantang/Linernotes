// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    property var albumId: 0
    signal backRequested()

    property int infoRevision: 0
    readonly property var info: {
        infoRevision;
        return AppContext.actions ? AppContext.actions.albumInfo(albumId) : ({})
    }
    property alias currentIndex: listView.currentIndex

    Connections {
        target: AppContext.marks
        function onMarksChanged() {
            root.infoRevision++
        }
    }

    Shortcut {
        sequence: "Esc"
        onActivated: root.backRequested()
    }

    Shortcut {
        sequence: "Backspace"
        onActivated: root.backRequested()
    }

    TrackListModel {
        id: trackModel
        context: AppContext
        albumId: root.albumId
    }

    TrackContextMenu {
        id: albumContextMenu
        Component.onCompleted: {
            playSource = PlaySource.Album
        }
    }

    function playAlbum(shuffle) {
        const ids = trackModel.allTrackIds()
        if (ids.length === 0 || !AppContext.actions) {
            return
        }
        if (shuffle && AppContext.player && AppContext.player.queue) {
            AppContext.player.queue.mode = PlayMode.Shuffle
        }
        AppContext.actions.playTracks(ids, 0, PlaySource.Album)
    }

    function playTrackAt(index) {
        const ids = trackModel.allTrackIds()
        if (ids.length === 0 || !AppContext.actions) {
            return
        }
        AppContext.actions.playTracks(ids, index, PlaySource.Album)
    }

    function formatMeta(info) {
        var parts = []
        if (info && info.year !== undefined && info.year !== null && info.year > 0) {
            parts.push(info.year)
        }
        if (info && info.trackCount !== undefined) {
            parts.push(qsTr("%n track(s)", "", info.trackCount))
        }
        if (info && info.durationText) {
            parts.push(info.durationText)
        }
        return parts.join(" · ")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Back button bar
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium

            Controls.IconButton {
                icon.source: "icons/arrow-left.svg"
                toolTip: qsTr("Back")
                onClicked: root.backRequested()
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Album Header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium

            // Big Cover
            Item {
                Layout.preferredWidth: Theme.albumDetailCoverSize
                Layout.preferredHeight: Theme.albumDetailCoverSize

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.coverBorderRadius
                    color: Theme.surfaceVariant
                    clip: true

                    Label {
                        anchors.centerIn: parent
                        visible: bigCoverImage.status !== Image.Ready || !(root.info && root.info.coverHash)
                        text: (root.info && root.info.title && root.info.title.length > 0)
                            ? root.info.title.charAt(0).toUpperCase() : "?"
                        font.pixelSize: Math.floor(Theme.albumDetailCoverSize * 0.4)
                        font.bold: true
                        color: Theme.textSecondary
                    }

                    Image {
                        id: bigCoverImage
                        anchors.fill: parent
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        source: (root.info && root.info.coverHash)
                            ? ("image://cover/" + encodeURIComponent(root.info.coverHash)) : ""
                        sourceSize: Qt.size(Theme.albumDetailCoverSize, Theme.albumDetailCoverSize)
                        visible: status === Image.Ready
                    }
                }
            }

            // Info and Buttons
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: (root.info && root.info.title) ? root.info.title : qsTr("Unknown Album")
                    font.pixelSize: Theme.fontSizeTitle
                    font.bold: true
                    color: Theme.text
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }

                Label {
                    text: (root.info && root.info.albumArtist) ? root.info.albumArtist : qsTr("Unknown Artist")
                    font.pixelSize: Theme.fontSizeLarge
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }

                Label {
                    text: root.formatMeta(root.info)
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }

                Item {
                    Layout.preferredHeight: Theme.spacingSmall
                }

                RowLayout {
                    spacing: Theme.spacingMedium

                    Controls.AppButton {
                        text: qsTr("Play")
                        primary: true
                        icon.source: "icons/play.svg"
                        onClicked: root.playAlbum(false)
                    }

                    Controls.AppButton {
                        text: qsTr("Shuffle")
                        icon.source: "icons/shuffle.svg"
                        onClicked: root.playAlbum(true)
                    }

                    Controls.FavoriteButton {
                        favorite: (root.info && root.info.favorite) ? true : false
                        onFavoriteToggled: {
                            if (AppContext.marks && root.albumId > 0) {
                                AppContext.marks.toggleFavorite(Library.FavoriteKind.Album, root.albumId)
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Track List Header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: (listView.ScrollBar.vertical.visible ? listView.ScrollBar.vertical.width : 0) + Theme.spacingMedium
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingSmall
            spacing: Theme.spacingMedium

            Label {
                text: "#"
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                Layout.preferredWidth: 32
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                //: Track title header
                text: qsTr("Title")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                Layout.fillWidth: true
            }

            Label {
                //: Track duration header
                text: qsTr("Duration")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                Layout.preferredWidth: 60
                horizontalAlignment: Text.AlignRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Track List
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            reuseItems: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            model: trackModel

            ScrollBar.vertical: Controls.AppScrollBar {}

            Keys.onReturnPressed: (event) => {
                if (currentIndex >= 0 && currentIndex < trackModel.count) {
                    root.playTrackAt(currentIndex)
                    event.accepted = true
                }
            }
            Keys.onEnterPressed: (event) => {
                if (currentIndex >= 0 && currentIndex < trackModel.count) {
                    root.playTrackAt(currentIndex)
                    event.accepted = true
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    if (currentIndex >= 0 && currentIndex < trackModel.count) {
                        const id = trackModel.trackIdAt(currentIndex)
                        if (id > 0) {
                            albumContextMenu.popupAt([id], currentItem || listView)
                            event.accepted = true
                        }
                    }
                }
            }

            delegate: AlbumTrackRow {
                albumDetail: root
                contextMenu: albumContextMenu
                trailingInset: listView.ScrollBar.vertical.visible ? listView.ScrollBar.vertical.width : 0
            }
        }
    }
}
