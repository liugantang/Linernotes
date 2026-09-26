// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

ToolButton {
    id: control
    property string toolTip: ""
    
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    
    icon.color: Theme.text
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    
    ToolTip.visible: toolTip !== "" && hovered
    ToolTip.text: toolTip
    ToolTip.delay: 500

    background: Rectangle {
        color: control.down ? Theme.itemSelected : (control.hovered ? Theme.hoverOverlay : "transparent")
        radius: width / 2
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: control.visualFocus ? Theme.focusRing : "transparent"
            border.width: 2
            radius: width / 2
            visible: control.visualFocus
        }
    }
}
