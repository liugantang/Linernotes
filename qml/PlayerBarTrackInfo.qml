// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    signal openAlbumRequested(var albumId)

    readonly property bool hasTrack: AppContext.nowPlaying ? AppContext.nowPlaying.hasTrack : false
    readonly property real albumId: AppContext.nowPlaying ? AppContext.nowPlaying.albumId : 0
    readonly property string coverHash: AppContext.nowPlaying ? AppContext.nowPlaying.coverHash : ""
    readonly property string title: AppContext.nowPlaying ? AppContext.nowPlaying.title : ""
    readonly property string artist: AppContext.nowPlaying ? AppContext.nowPlaying.artist : ""
    readonly property bool canOpenAlbum: root.hasTrack && (root.albumId > 0)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        Item {
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                id: coverRect
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: Theme.surfaceVariant
                clip: true

                Image {
                    id: fallbackIcon
                    anchors.centerIn: parent
                    source: "icons/music.svg"
                    width: 24
                    height: 24
                    visible: !coverImage.visible
                    opacity: 0.6
                }

                Image {
                    id: coverImage
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: root.coverHash.length > 0 ? ("image://cover/" + encodeURIComponent(root.coverHash)) : ""
                    sourceSize: Qt.size(112, 112)
                    visible: status === Image.Ready && root.coverHash.length > 0
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: root.canOpenAlbum ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (root.canOpenAlbum) {
                        root.openAlbumRequested(root.albumId)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingTiny

            Label {
                id: titleLabel
                text: root.hasTrack ? (root.title.length > 0 ? root.title : qsTr("Unknown Title")) : qsTr("Not playing")
                font.pixelSize: Theme.fontSizeNormal
                font.bold: root.hasTrack
                color: root.hasTrack ? Theme.text : Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
                maximumLineCount: 1

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: root.canOpenAlbum ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        if (root.canOpenAlbum) {
                            root.openAlbumRequested(root.albumId)
                        }
                    }
                }
            }

            Label {
                id: artistLabel
                visible: root.hasTrack
                text: root.artist.length > 0 ? root.artist : qsTr("Unknown Artist")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
                maximumLineCount: 1
            }
        }
    }
}
