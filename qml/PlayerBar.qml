// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Rectangle {
    id: root

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    readonly property bool hasCurrentSource: AppContext.player ? AppContext.player.currentSource.length > 0 : false
    readonly property bool isPlaying: AppContext.player ? AppContext.player.state === Player.Playing : false

    function extractFileName(path) {
        if (!path || path.length === 0) {
            return qsTr("No track playing")
        }
        const parts = path.split("/")
        return parts.length > 0 ? parts[parts.length - 1] : path
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Controls
        RowLayout {
            spacing: Theme.spacingSmall

            Button {
                text: qsTr("Previous")
                enabled: root.hasCurrentSource
                onClicked: {
                    if (AppContext.player) {
                        AppContext.player.previous()
                    }
                }
            }

            Button {
                text: root.isPlaying ? qsTr("Pause") : qsTr("Play")
                enabled: root.hasCurrentSource
                onClicked: {
                    if (AppContext.player) {
                        AppContext.player.togglePause()
                    }
                }
            }

            Button {
                text: qsTr("Next")
                enabled: root.hasCurrentSource
                onClicked: {
                    if (AppContext.player) {
                        AppContext.player.next()
                    }
                }
            }
        }

        // Track info
        Label {
            text: root.extractFileName(AppContext.player ? AppContext.player.currentSource : "")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.text
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
