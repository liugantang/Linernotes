// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes

ColumnLayout {
    id: root
    spacing: Theme.spacingLarge

    // Section title & description
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingTiny

        Label {
            text: qsTr("AI")
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Linernotes does not ship any API key. Connect an OpenAI-compatible service (cloud, or local such as Ollama / llama.cpp). Audio never leaves your computer.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    SettingsAiServices {
        Layout.fillWidth: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.divider
    }

    SettingsAiRoutes {
        Layout.fillWidth: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.divider
    }

    SettingsAiPrivacy {
        Layout.fillWidth: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.divider
    }

    SettingsAiUsage {
        Layout.fillWidth: true
    }
}
