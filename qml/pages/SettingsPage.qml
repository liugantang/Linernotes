// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: Math.min(scrollView.availableWidth - Theme.spacingLarge * 2, 720)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.spacingLarge

            Item {
                Layout.preferredHeight: Theme.spacingMedium
            }

            // Top title
            Label {
                text: qsTr("Settings")
                font.pixelSize: Theme.fontSizeTitle
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            SettingsLibrarySection {
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            SettingsPlaybackSection {
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            SettingsAppearanceSection {
                Layout.fillWidth: true
            }

            Item {
                Layout.preferredHeight: Theme.spacingExtraLarge
            }
        }
    }
}
