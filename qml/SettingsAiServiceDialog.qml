// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    property bool isEdit: false
    property string serviceId: ""
    property bool keyPresent: false
    property string errorMessage: ""

    readonly property var presetsModel: [
        { text: qsTr("Custom"), name: "", baseUrl: "" },
        { text: "OpenAI", name: "OpenAI", baseUrl: "https://api.openai.com/v1" },
        { text: "DeepSeek", name: "DeepSeek", baseUrl: "https://api.deepseek.com/v1" },
        { text: "Ollama (local)", name: "Ollama", baseUrl: "http://127.0.0.1:11434/v1" },
        { text: "llama.cpp (local)", name: "llama.cpp", baseUrl: "http://127.0.0.1:8080/v1" }
    ]

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    implicitWidth: 480
    implicitHeight: layout.implicitHeight + topPadding + bottomPadding
    padding: Theme.spacingMedium

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.divider
        border.width: 1
        radius: Theme.cardBorderRadius
    }

    function openNew() {
        isEdit = false
        serviceId = ""
        keyPresent = false
        errorMessage = ""
        presetCombo.currentIndex = 0
        nameField.text = ""
        baseUrlField.text = ""
        modelField.text = ""
        keyField.text = ""
        clearKeyCheck.checked = false
        timeoutSpin.value = 60
        maxConcurrentSpin.value = 2
        rpmSpin.value = 0
        open()
        nameField.forceActiveFocus()
    }

    function openEdit(id, name, baseUrl, defaultModel, timeoutMs, maxConcurrent, requestsPerMinute) {
        isEdit = true
        serviceId = id
        keyPresent = false
        errorMessage = ""
        presetCombo.currentIndex = 0
        nameField.text = name || ""
        baseUrlField.text = baseUrl || ""
        modelField.text = defaultModel || ""
        keyField.text = ""
        clearKeyCheck.checked = false
        timeoutSpin.value = Math.max(1, Math.round((timeoutMs || 60000) / 1000))
        maxConcurrentSpin.value = maxConcurrent > 0 ? maxConcurrent : 2
        rpmSpin.value = requestsPerMinute >= 0 ? requestsPerMinute : 0
        open()
        if (AppContext.aiSettings) {
            AppContext.aiSettings.checkApiKey(id)
        }
        nameField.forceActiveFocus()
    }

    function save() {
        errorMessage = ""
        const id = isEdit ? serviceId : ""
        const name = nameField.text.trim()
        const baseUrl = baseUrlField.text.trim()
        const model = modelField.text.trim()
        const timeoutMs = timeoutSpin.value * 1000
        const maxConc = maxConcurrentSpin.value
        const rpm = rpmSpin.value

        if (!AppContext.aiSettings) {
            return
        }

        const savedId = AppContext.aiSettings.saveService(
            id, name, baseUrl, model, timeoutMs, maxConc, rpm)

        if (savedId.length === 0) {
            return
        }

        if (clearKeyCheck.checked) {
            AppContext.aiSettings.setApiKey(savedId, "")
        } else if (keyField.text.trim().length > 0) {
            AppContext.aiSettings.setApiKey(savedId, keyField.text.trim())
        }

        root.close()
    }

    Connections {
        target: AppContext.aiSettings
        function onApiKeyChecked(checkedId, present) {
            if (root.visible && root.isEdit && root.serviceId === checkedId) {
                root.keyPresent = present
            }
        }
        function onErrorOccurred(message) {
            if (root.visible) {
                root.errorMessage = message
            }
        }
    }

    contentItem: ColumnLayout {
        id: layout
        spacing: Theme.spacingMedium

        Label {
            text: root.isEdit ? qsTr("Edit Service") : qsTr("Add Service")
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: Theme.spacingMedium
            rowSpacing: Theme.spacingSmall
            Layout.fillWidth: true

            // Preset
            Label {
                text: qsTr("Preset")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.preferredWidth: 120
            }

            Controls.AppComboBox {
                id: presetCombo
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "name"
                model: root.presetsModel
                onActivated: {
                    if (currentIndex > 0) {
                        const item = root.presetsModel[currentIndex]
                        nameField.text = item.name
                        baseUrlField.text = item.baseUrl
                    }
                }
            }

            // Name
            Label {
                text: qsTr("Name")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.text
                Layout.preferredWidth: 120
            }

            Controls.AppTextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. OpenAI")
            }

            // Base URL
            Label {
                text: qsTr("Base URL")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.text
                Layout.preferredWidth: 120
            }

            Controls.AppTextField {
                id: baseUrlField
                Layout.fillWidth: true
                placeholderText: "https://api.openai.com/v1"
            }

            // Default Model
            Label {
                text: qsTr("Default Model")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.text
                Layout.preferredWidth: 120
            }

            Controls.AppTextField {
                id: modelField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. gpt-4o, llama3")
            }

            // API Key
            Label {
                text: qsTr("API Key")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.text
                Layout.preferredWidth: 120
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTiny

                Controls.AppTextField {
                    id: keyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    enabled: !clearKeyCheck.checked
                    placeholderText: {
                        if (root.isEdit) {
                            return root.keyPresent ? qsTr("Saved — leave empty to keep") : qsTr("Not set")
                        }
                        return qsTr("Not set")
                    }
                }

                Controls.AppCheckBox {
                    id: clearKeyCheck
                    text: qsTr("Clear key")
                    visible: root.isEdit && root.keyPresent
                }
            }

            // Timeout
            Label {
                text: qsTr("Timeout (seconds)")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.preferredWidth: 120
            }

            Controls.AppSpinBox {
                id: timeoutSpin
                from: 1
                to: 600
                value: 60
                Layout.preferredWidth: 100
            }

            // Max concurrent
            Label {
                text: qsTr("Max concurrent")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.preferredWidth: 120
            }

            Controls.AppSpinBox {
                id: maxConcurrentSpin
                from: 1
                to: 16
                value: 2
                Layout.preferredWidth: 100
            }

            // Requests per minute
            Label {
                text: qsTr("Requests / min")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.preferredWidth: 120
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Controls.AppSpinBox {
                    id: rpmSpin
                    from: 0
                    to: 10000
                    value: 0
                    Layout.preferredWidth: 100
                }

                Label {
                    text: qsTr("0 = unlimited")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                }
            }
        }

        // Error message
        Label {
            visible: root.errorMessage.length > 0
            text: root.errorMessage
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.errorText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // Buttons
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacingSmall

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            Controls.AppButton {
                primary: true
                text: qsTr("Save")
                onClicked: root.save()
            }
        }
    }
}
