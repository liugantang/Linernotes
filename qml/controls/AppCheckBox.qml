// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

CheckBox {
    id: control

    implicitHeight: Theme.controlHeight
    implicitWidth: indicator.implicitWidth + spacing + contentItem.implicitWidth

    font.pixelSize: Theme.fontSizeNormal
    spacing: Theme.spacingSmall

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: Theme.radiusSmall
        color: control.checked ? (control.down ? Theme.accentHover : Theme.accent) : "transparent"
        border.color: control.checked ? Theme.accent : (control.hovered ? Theme.textSecondary : Theme.divider)
        border.width: 1

        Canvas {
            id: checkCanvas
            anchors.fill: parent
            anchors.margins: 2
            visible: control.checked
            contextType: "2d"

            Connections {
                target: control
                function onCheckStateChanged() {
                    checkCanvas.requestPaint()
                }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = Theme.accentText
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(width * 0.2, height * 0.5)
                ctx.lineTo(width * 0.45, height * 0.75)
                ctx.lineTo(width * 0.8, height * 0.25)
                ctx.stroke()
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: control.visualFocus ? Theme.focusRing : "transparent"
            border.width: 2
            radius: Theme.radiusSmall + 2
            visible: control.visualFocus
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textSecondary
        verticalAlignment: Text.AlignVCenter
    }
}
