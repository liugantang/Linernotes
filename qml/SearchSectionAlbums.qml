// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

ColumnLayout {
    id: root

    signal albumSelected(var albumId)
    signal playAlbumRequested(var albumId)

    spacing: Theme.spacingSmall

    Label {
        text: qsTr("Albums")
        font.pixelSize: Theme.fontSizeLarge
        font.bold: true
        color: Theme.text
    }

    ListView {
        id: albumsRow
        Layout.fillWidth: true
        implicitHeight: 190
        orientation: ListView.Horizontal
        spacing: Theme.spacingMedium
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: AppContext.search ? AppContext.search.albums : []

        delegate: Item {
            id: albumDelegate
            required property int index
            required property var modelData

            width: 130
            height: 185

            Rectangle {
                id: albumCover
                width: 130
                height: 130
                radius: Theme.coverBorderRadius
                color: Theme.surfaceVariant
                clip: true

                Label {
                    anchors.centerIn: parent
                    visible: albumImg.status !== Image.Ready || !albumDelegate.modelData.coverHash
                    text: (albumDelegate.modelData.title && albumDelegate.modelData.title.length > 0)
                        ? albumDelegate.modelData.title.charAt(0).toUpperCase() : "?"
                    font.pixelSize: 32
                    font.bold: true
                    color: Theme.textSecondary
                }

                Image {
                    id: albumImg
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: albumDelegate.modelData.coverHash
                        ? ("image://cover/" + encodeURIComponent(albumDelegate.modelData.coverHash)) : ""
                    sourceSize: Qt.size(130, 130)
                    visible: status === Image.Ready
                }

                // Hover play button
                Rectangle {
                    visible: albumMouseArea.containsMouse
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingSmall
                    width: 32
                    height: 32
                    radius: 16
                    color: Theme.accent

                    Label {
                        anchors.centerIn: parent
                        text: "▶"
                        color: "#ffffff"
                        font.pixelSize: Theme.fontSizeSmall
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: (event) => {
                            event.accepted = true
                            root.playAlbumRequested(albumDelegate.modelData.albumId)
                        }
                    }
                }
            }

            Label {
                id: albumTitleLabel
                anchors.top: albumCover.bottom
                anchors.topMargin: Theme.spacingTiny
                anchors.left: parent.left
                anchors.right: parent.right
                text: albumDelegate.modelData.title || qsTr("Unknown Album")
                font.pixelSize: Theme.fontSizeNormal
                font.bold: true
                color: albumMouseArea.containsMouse ? Theme.accent : Theme.text
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Label {
                anchors.top: albumTitleLabel.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                text: albumDelegate.modelData.albumArtist || qsTr("Unknown Artist")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            MouseArea {
                id: albumMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.albumSelected(albumDelegate.modelData.albumId)
                }
            }
        }
    }
}
