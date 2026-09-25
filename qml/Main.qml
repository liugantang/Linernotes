// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

ApplicationWindow {
    id: window

    title: qsTr("Linernotes")
    width: Theme.windowDefaultWidth
    height: Theme.windowDefaultHeight
    minimumWidth: Theme.windowMinWidth
    minimumHeight: Theme.windowMinHeight
    visible: true
    color: Theme.background

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Theme.spacingMedium

        Label {
            text: AppInfo.name
            font.pixelSize: Theme.fontSizeTitle
            font.bold: true
            color: Theme.text
            Layout.alignment: Qt.AlignHCenter
        }

        Label {
            text: qsTr("Version %1").arg(AppInfo.version)
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.textSecondary
            Layout.alignment: Qt.AlignHCenter
        }
    }
}
