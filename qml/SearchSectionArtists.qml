// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Linernotes

ColumnLayout {
    id: root

    signal artistSelected(var artistId)

    spacing: Theme.spacingSmall

    Label {
        text: qsTr("Artists")
        font.pixelSize: Theme.fontSizeLarge
        font.bold: true
        color: Theme.text
    }

    ListView {
        id: artistsRow
        Layout.fillWidth: true
        implicitHeight: 100
        orientation: ListView.Horizontal
        spacing: Theme.spacingMedium
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        model: AppContext.search ? AppContext.search.artists : []

        delegate: Item {
            id: artistDelegate
            required property int index
            required property var modelData

            width: 80
            height: 100

            Rectangle {
                id: artistAvatar
                width: Theme.artistAvatarSizeSmall + 12
                height: Theme.artistAvatarSizeSmall + 12
                radius: width / 2
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.surfaceVariant
                clip: true
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: artistAvatarMask
                }

                Item {
                    id: artistAvatarMask
                    anchors.fill: parent
                    layer.enabled: true
                    visible: false

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "black"
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: avatarImg.status !== Image.Ready || !artistDelegate.modelData.coverHash
                    text: (artistDelegate.modelData.name && artistDelegate.modelData.name.length > 0)
                        ? artistDelegate.modelData.name.charAt(0).toUpperCase() : "?"
                    font.pixelSize: 20
                    font.bold: true
                    color: Theme.textSecondary
                }

                Image {
                    id: avatarImg
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: artistDelegate.modelData.coverHash
                        ? ("image://cover/" + encodeURIComponent(artistDelegate.modelData.coverHash)) : ""
                    sourceSize: Qt.size(Theme.artistAvatarSizeSmall + 12, Theme.artistAvatarSizeSmall + 12)
                    visible: status === Image.Ready
                }
            }

            Label {
                anchors.top: artistAvatar.bottom
                anchors.topMargin: Theme.spacingTiny
                anchors.left: parent.left
                anchors.right: parent.right
                text: artistDelegate.modelData.name || qsTr("Unknown Artist")
                font.pixelSize: Theme.fontSizeSmall
                color: artistMouseArea.containsMouse ? Theme.accent : Theme.text
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            MouseArea {
                id: artistMouseArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.artistSelected(artistDelegate.modelData.artistId)
                }
            }
        }
    }
}
