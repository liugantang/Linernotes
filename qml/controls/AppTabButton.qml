// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

TabButton {
    id: control
    
    implicitHeight: Theme.controlHeight
    
    font.pixelSize: Theme.fontSizeNormal
    font.weight: checked ? Font.DemiBold : Font.Normal
    
    // Basic 样式的 TabButton 未选中时用 palette.brightText（白色）绘制文字，这里显式指定
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? Theme.accent : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: control.down ? Theme.hoverOverlay : (control.hovered ? Theme.hoverOverlay : "transparent")
        
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 2
            color: Theme.accent
            visible: control.checked
        }
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: control.visualFocus ? Theme.focusRing : "transparent"
            border.width: 2
            visible: control.visualFocus
        }
    }
}
