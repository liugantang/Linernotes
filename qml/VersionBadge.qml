// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

Rectangle {
    id: root

    property int versionType: -1

    readonly property bool hasBadge: versionType >= 0 && versionType !== Library.VersionType.Studio

    visible: hasBadge
    implicitWidth: hasBadge ? (badgeLabel.implicitWidth + Theme.spacingSmall) : 0
    implicitHeight: hasBadge ? 18 : 0

    color: Theme.surfaceVariant
    radius: Theme.radiusSmall

    function versionBadgeText(vt) {
        if (vt === Library.VersionType.Live) return qsTr("Live")
        if (vt === Library.VersionType.Remaster) return qsTr("Remaster")
        if (vt === Library.VersionType.Acoustic) return qsTr("Acoustic")
        if (vt === Library.VersionType.Remix) return qsTr("Remix")
        if (vt === Library.VersionType.Demo) return qsTr("Demo")
        if (vt === Library.VersionType.Instrumental) return qsTr("Instrumental")
        if (vt === Library.VersionType.Edit) return qsTr("Edit")
        if (vt === Library.VersionType.Alternate) return qsTr("Alt. version")
        return ""
    }

    Label {
        id: badgeLabel
        anchors.centerIn: parent
        text: root.versionBadgeText(root.versionType)
        font.pixelSize: Theme.fontSizeSmall - 1
        color: Theme.textSecondary
    }
}
