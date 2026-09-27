// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

ToolButton {
    id: control
    property bool favorite: false
    signal favoriteToggled()

    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight

    icon.source: favorite ? Qt.resolvedUrl("../icons/heart-filled.svg") : Qt.resolvedUrl("../icons/heart.svg")
    icon.color: favorite ? Theme.favorite : (hovered ? Theme.text : Theme.textSecondary)
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize

    ToolTip.visible: hovered
    ToolTip.text: favorite ? qsTr("Remove from Loved") : qsTr("Love")
    ToolTip.delay: 500

    onClicked: control.favoriteToggled()

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
