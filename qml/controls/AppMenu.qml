// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: Theme.spacingTiny
    topPadding: Theme.spacingTiny
    bottomPadding: Theme.spacingTiny
    leftPadding: Theme.spacingTiny
    rightPadding: Theme.spacingTiny

    delegate: AppMenuItem { }

    background: Rectangle {
        implicitWidth: Theme.menuMinWidth
        implicitHeight: Theme.controlHeight
        color: Theme.surface
        border.color: Theme.divider
        border.width: 1
        radius: Theme.radiusMedium
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: false
        clip: true
        currentIndex: control.currentIndex
        ScrollIndicator.vertical: ScrollIndicator { }
    }
}
