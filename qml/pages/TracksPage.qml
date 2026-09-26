// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Library")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            Label {
                text: "·"
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.textSecondary
            }

            Label {
                text: qsTr("%n track(s)", "", trackTable.count)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        TrackTable {
            id: trackTable
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
