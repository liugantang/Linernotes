// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        Label {
            text: qsTr("AI")
            font.pixelSize: Theme.fontSizeTitle
            font.bold: true
            color: Theme.text
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Smart assistant and music generation (Coming soon).")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
