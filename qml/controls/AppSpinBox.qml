// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

SpinBox {
    id: control

    implicitHeight: Theme.controlHeight
    implicitWidth: 90
    editable: true

    font.pixelSize: Theme.fontSizeNormal

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        font.pixelSize: Theme.fontSizeNormal
        color: control.enabled ? Theme.text : Theme.textSecondary
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhDigitsOnly
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        height: parent.height
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        color: !control.enabled ? "transparent" : (control.up.pressed ? Theme.itemSelected : (control.up.hovered ? Theme.hoverOverlay : "transparent"))
        radius: Theme.radiusSmall

        Text {
            text: "+"
            font.pixelSize: Theme.fontSizeNormal
            color: !control.enabled ? Theme.textSecondary : (control.up.pressed ? Theme.accent : Theme.text)
            anchors.fill: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? control.width - width : 0
        height: parent.height
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        color: !control.enabled ? "transparent" : (control.down.pressed ? Theme.itemSelected : (control.down.hovered ? Theme.hoverOverlay : "transparent"))
        radius: Theme.radiusSmall

        Text {
            text: "-"
            font.pixelSize: Theme.fontSizeNormal
            color: !control.enabled ? Theme.textSecondary : (control.down.pressed ? Theme.accent : Theme.text)
            anchors.fill: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        color: control.enabled ? Theme.surfaceVariant : Theme.divider
        radius: Theme.radiusMedium
        border.color: (control.enabled && control.activeFocus) ? Theme.accent : "transparent"
        border.width: 1
    }
}
