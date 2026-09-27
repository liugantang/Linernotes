// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root
    spacing: Theme.spacingSmall

    // Subtitle
    Label {
        text: qsTr("Privacy")
        font.pixelSize: Theme.fontSizeNormal
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }

    // Play history checkbox
    Controls.AppCheckBox {
        id: historyCheck
        text: qsTr("Send play history to cloud services")
        checked: AppContext.aiSettings ? AppContext.aiSettings.playHistoryAllowed : true
        onToggled: {
            if (AppContext.aiSettings) {
                AppContext.aiSettings.playHistoryAllowed = checked
            }
        }
    }

    // Moments checkbox
    Controls.AppCheckBox {
        id: momentsCheck
        text: qsTr("Send moments to cloud services")
        checked: AppContext.aiSettings ? AppContext.aiSettings.momentsAllowed : false
        onToggled: {
            if (AppContext.aiSettings) {
                AppContext.aiSettings.momentsAllowed = checked
            }
        }
    }

    // Location checkbox
    Controls.AppCheckBox {
        id: locationCheck
        text: qsTr("Send location to cloud services")
        checked: AppContext.aiSettings ? AppContext.aiSettings.locationAllowed : false
        onToggled: {
            if (AppContext.aiSettings) {
                AppContext.aiSettings.locationAllowed = checked
            }
        }
    }

    Connections {
        target: AppContext.aiSettings
        function onPrivacyChanged() {
            if (!AppContext.aiSettings) return
            historyCheck.checked = AppContext.aiSettings.playHistoryAllowed
            momentsCheck.checked = AppContext.aiSettings.momentsAllowed
            locationCheck.checked = AppContext.aiSettings.locationAllowed
        }
    }

    // Explanatory note
    Label {
        text: qsTr("Local services (localhost / LAN) are not restricted. Only text metadata is ever sent; audio is never uploaded.")
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
