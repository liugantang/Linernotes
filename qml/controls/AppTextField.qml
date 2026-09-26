// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

TextField {
    id: control

    implicitHeight: Theme.controlHeight
    implicitWidth: 200

    font.pixelSize: Theme.fontSizeNormal
    color: Theme.text
    selectedTextColor: Theme.accentText
    selectionColor: Theme.accent
    placeholderTextColor: Theme.textSecondary
    verticalAlignment: TextInput.AlignVCenter

    leftPadding: Theme.spacingSmall
    rightPadding: Theme.spacingSmall

    background: Rectangle {
        color: Theme.surfaceVariant
        radius: Theme.radiusMedium
        border.color: control.activeFocus ? Theme.accent : "transparent"
        border.width: 1
    }
}
