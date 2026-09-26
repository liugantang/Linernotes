// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

Slider {
    id: control

    property real playbackPosition: 0
    property color trackColor: Theme.surfaceVariant
    property color progressColor: Theme.accent
    property color handleColor: Theme.accentText
    property int trackHeightNormal: 4
    property int trackHeightHovered: 6
    property int handleDiameter: 12
    property bool wasPressed: false

    signal seekRequested(real position)

    implicitWidth: 200
    implicitHeight: Theme.controlHeight

    hoverEnabled: true

    onPlaybackPositionChanged: {
        if (!pressed) {
            value = playbackPosition
        }
    }

    onPressedChanged: {
        if (pressed) {
            wasPressed = true
        } else if (wasPressed) {
            wasPressed = false
            seekRequested(value)
        }
    }

    background: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: (control.hovered || control.pressed) ? control.trackHeightHovered : control.trackHeightNormal

        Behavior on height {
            NumberAnimation {
                duration: 150
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: control.trackColor
        }

        Rectangle {
            x: 0
            y: 0
            width: Math.max(0, Math.min(parent.width, control.visualPosition * parent.width))
            height: parent.height
            radius: height / 2
            color: control.enabled ? control.progressColor : Theme.textSecondary
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: control.handleDiameter
        implicitHeight: control.handleDiameter
        radius: control.handleDiameter / 2
        color: control.handleColor
        border.color: Theme.accent
        border.width: 1
        visible: control.enabled && (control.hovered || control.pressed)
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
