// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

Button {
    id: control
    property bool primary: false

    implicitHeight: Theme.controlHeight
    
    icon.color: primary ? Theme.accentText : Theme.text
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    
    palette.buttonText: primary ? Theme.accentText : Theme.text
    font.pixelSize: Theme.fontSizeNormal
    font.weight: primary ? Font.DemiBold : Font.Normal

    background: Rectangle {
        implicitWidth: 80
        color: {
            if (!control.enabled) return Theme.divider
            if (control.primary) {
                return control.down ? Theme.accentHover : (control.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
            } else {
                return control.down ? Theme.itemSelected : (control.hovered ? Theme.hoverOverlay : "transparent")
            }
        }
        radius: Theme.radiusMedium

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: control.visualFocus ? Theme.focusRing : "transparent"
            border.width: 2
            radius: Theme.radiusMedium + 2
            visible: control.visualFocus
        }
    }
}
