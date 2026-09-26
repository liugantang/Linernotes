// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

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

            Controls.IconButton {
                icon.source: "icons/skip-back.svg"
                toolTip: qsTr("Previous")
                enabled: root.hasCurrentSource
                onClicked: {
                    if (AppContext.player) {
                        AppContext.player.previous()
                    }
                }
            }

            Controls.IconButton {
                icon.source: root.isPlaying ? "icons/pause.svg" : "icons/play.svg"
                toolTip: root.isPlaying ? qsTr("Pause") : qsTr("Play")
                enabled: root.hasCurrentSource
                implicitWidth: Theme.controlHeight * 1.2
                implicitHeight: Theme.controlHeight * 1.2
                icon.width: Theme.iconSize * 1.2
                icon.height: Theme.iconSize * 1.2
                icon.color: Theme.accentText
                
                background: Rectangle {
                    color: parent.down ? Theme.accentHover : (parent.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
                    radius: width / 2
                    
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        color: "transparent"
                        border.color: parent.parent.visualFocus ? Theme.focusRing : "transparent"
                        border.width: 2
                        radius: width / 2
                        visible: parent.parent.visualFocus
                    }
                }
                
                onClicked: {
                    if (AppContext.player) {
                        AppContext.player.togglePause()
                    }
                }
            }

            Controls.IconButton {
                icon.source: "icons/skip-forward.svg"
                toolTip: qsTr("Next")
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
