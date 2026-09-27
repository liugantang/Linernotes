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

    Label {
        text: qsTr("Developer")
        font.pixelSize: Theme.fontSizeNormal
        font.bold: true
        color: Theme.text
    }

    Controls.AppCheckBox {
        id: developerCheckBox
        text: qsTr("Record LLM requests and responses (developer mode)")
        checked: (AppContext.llmDebug && AppContext.llmDebug.enabled) || false
        onToggled: {
            if (AppContext.llmDebug) {
                AppContext.llmDebug.enabled = checked
            }
        }
    }

    Label {
        text: qsTr("Full request and response text is kept in memory only while this is on.")
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        leftPadding: 26
    }

    Controls.AppButton {
        id: openPanelBtn
        visible: (AppContext.llmDebug && AppContext.llmDebug.enabled) || false
        text: qsTr("Open LLM debug panel")
        onClicked: {
            debugPanelLoader.active = true
            if (debugPanelLoader.item) {
                debugPanelLoader.item.show()
                debugPanelLoader.item.raise()
                debugPanelLoader.item.requestActivate()
            }
        }
    }

    Loader {
        id: debugPanelLoader
        active: false
        source: "LlmDebugPanel.qml"
        onLoaded: {
            if (item) {
                item.show()
                item.raise()
                item.requestActivate()
            }
        }
    }
}
