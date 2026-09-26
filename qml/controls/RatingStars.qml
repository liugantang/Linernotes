// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Linernotes

Item {
    id: control

    property int rating: 0
    property int starSize: 14
    property int spacing: 2

    signal rated(int value)

    property int hoverRating: 0
    readonly property int effectiveRating: hoverRating > 0 ? hoverRating : rating

    implicitWidth: 5 * starSize + 4 * spacing
    implicitHeight: starSize

    Row {
        anchors.centerIn: parent
        spacing: control.spacing

        Repeater {
            model: 5

            delegate: Item {
                id: starItem
                required property int index
                readonly property int starValue: index + 1
                readonly property bool isFilled: starValue <= control.effectiveRating

                width: control.starSize
                height: control.starSize

                IconImage {
                    anchors.fill: parent
                    source: starItem.isFilled ? Qt.resolvedUrl("../icons/star-filled.svg") : Qt.resolvedUrl("../icons/star.svg")
                    sourceSize: Qt.size(control.starSize, control.starSize)
                    color: starItem.isFilled ? Theme.rating : (control.hoverRating > 0 ? Theme.divider : (control.rating > 0 ? Theme.divider : Theme.textSecondary))
                    opacity: starItem.isFilled ? 1.0 : (control.hoverRating > 0 ? 0.7 : (control.rating > 0 ? 0.4 : (starMouseArea.containsMouse ? 0.5 : 0.2)))
                }
            }
        }
    }

    MouseArea {
        id: starMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton

        function calculateStarIndex(mouseX) {
            const step = control.width / 5.0
            if (step <= 0) return 1
            const idx = Math.floor(mouseX / step) + 1
            return Math.max(1, Math.min(5, idx))
        }

        onPositionChanged: (mouse) => {
            control.hoverRating = calculateStarIndex(mouse.x)
        }

        onExited: {
            control.hoverRating = 0
        }

        onCanceled: {
            control.hoverRating = 0
        }

        onClicked: (mouse) => {
            const target = calculateStarIndex(mouse.x)
            if (target === control.rating) {
                control.rated(0)
            } else {
                control.rated(target)
            }
        }
    }
}
