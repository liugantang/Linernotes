// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: Theme.spacingTiny
    topPadding: Theme.spacingTiny
    bottomPadding: Theme.spacingTiny
    leftPadding: Theme.spacingTiny
    rightPadding: Theme.spacingTiny

    contentItem: Rectangle {
        implicitWidth: 140
        implicitHeight: 1
        color: Theme.divider
    }

    background: Rectangle {
        color: "transparent"
    }
}
