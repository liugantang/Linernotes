// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    function syncThemeIndex() {
        if (AppContext.settings) {
            const idx = themeCombo.indexOfValue(AppContext.settings.themeMode)
            if (idx >= 0 && idx !== themeCombo.currentIndex) {
                themeCombo.currentIndex = idx
            }
        }
    }

    Component.onCompleted: {
        syncThemeIndex()
    }

    Connections {
        target: AppContext.settings
        function onThemeModeChanged() {
            root.syncThemeIndex()
        }
    }

    // Section title
    Label {
        text: qsTr("Appearance")
        font.pixelSize: Theme.fontSizeLarge
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }

    // Theme mode
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        Label {
            text: qsTr("Theme")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.text
            Layout.preferredWidth: 140
        }

        Controls.AppComboBox {
            id: themeCombo
            Layout.preferredWidth: 200
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("System"), value: SettingsController.System },
                { text: qsTr("Light"), value: SettingsController.Light },
                { text: qsTr("Dark"), value: SettingsController.Dark }
            ]
            onActivated: {
                if (AppContext.settings) {
                    AppContext.settings.themeMode = currentValue
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    // Accent color from cover
    Controls.AppCheckBox {
        id: accentCheck
        text: qsTr("Accent color from album cover")
        checked: AppContext.settings ? AppContext.settings.accentFromCover : false
        onToggled: {
            if (AppContext.settings) {
                AppContext.settings.accentFromCover = checked
            }
        }

        Connections {
            target: AppContext.settings
            function onAccentFromCoverChanged() {
                accentCheck.checked = AppContext.settings.accentFromCover
            }
        }
    }
}
