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

    property var testResults: ({})
    property string removeTargetId: ""
    property string removeTargetName: ""

    Connections {
        target: AppContext.aiSettings
        function onServiceTested(serviceId, ok, message, latencyMs) {
            const res = Object.assign({}, root.testResults)
            res[serviceId] = { ok: ok, message: message, latencyMs: latencyMs }
            root.testResults = res
        }
    }

    // Confirmation Popup for removing a service
    Popup {
        id: confirmRemoveDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        implicitWidth: 380
        implicitHeight: removeLayout.implicitHeight + topPadding + bottomPadding
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

        contentItem: ColumnLayout {
            id: removeLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Remove Service")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Remove \"%1\"? Any feature routing configured to this service will fall back to the default service.").arg(root.removeTargetName)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Item {
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: confirmRemoveDialog.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Remove")
                    onClicked: {
                        confirmRemoveDialog.close()
                        if (AppContext.aiSettings) {
                            AppContext.aiSettings.removeService(root.removeTargetId)
                        }
                    }
                }
            }
        }
    }

    SettingsAiServiceDialog {
        id: serviceDialog
    }

    // Subtitle
    Label {
        text: qsTr("Services")
        font.pixelSize: Theme.fontSizeNormal
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }

    // Services list or empty hint
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Repeater {
            id: servicesRepeater
            model: AppContext.aiSettings ? AppContext.aiSettings.services : null

            delegate: Rectangle {
                id: serviceCard
                Layout.fillWidth: true
                implicitHeight: cardLayout.implicitHeight + Theme.spacingSmall * 2
                color: Theme.surfaceVariant
                radius: Theme.radiusMedium
                border.color: Theme.divider
                border.width: 1

                required property string serviceId
                required property string name
                required property string baseUrl
                required property string defaultModel
                required property int timeoutMs
                required property int maxConcurrent
                required property int requestsPerMinute
                required property bool isDefault
                required property bool isLocal
                required property bool capabilitiesKnown
                required property bool supportsJsonSchema
                required property bool supportsTools
                required property bool supportsJsonObject

                readonly property bool isTesting: AppContext.aiSettings
                    ? AppContext.aiSettings.testingServiceId === serviceId
                    : false
                readonly property bool anyTesting: AppContext.aiSettings
                    ? AppContext.aiSettings.testingServiceId !== ""
                    : false
                readonly property var testResult: root.testResults[serviceId]

                ColumnLayout {
                    id: cardLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSmall
                    spacing: Theme.spacingSmall

                    // Top row: Name, badges, buttons
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Label {
                            text: serviceCard.name
                            font.pixelSize: Theme.fontSizeNormal
                            font.bold: true
                            color: Theme.text
                        }

                        // Default badge
                        Rectangle {
                            visible: serviceCard.isDefault
                            color: Theme.accent
                            radius: Theme.radiusSmall
                            implicitHeight: 18
                            implicitWidth: defaultText.implicitWidth + Theme.spacingSmall
                            Text {
                                id: defaultText
                                anchors.centerIn: parent
                                text: qsTr("Default")
                                font.pixelSize: Theme.fontSizeSmall
                                font.bold: true
                                color: Theme.accentText
                            }
                        }

                        // Local badge
                        Rectangle {
                            visible: serviceCard.isLocal
                            color: Theme.surface
                            border.color: Theme.divider
                            border.width: 1
                            radius: Theme.radiusSmall
                            implicitHeight: 18
                            implicitWidth: localText.implicitWidth + Theme.spacingSmall
                            Text {
                                id: localText
                                anchors.centerIn: parent
                                text: qsTr("Local")
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                            }
                        }

                        // Capability badges
                        Rectangle {
                            visible: !serviceCard.capabilitiesKnown
                            color: Theme.surface
                            border.color: Theme.divider
                            border.width: 1
                            radius: Theme.radiusSmall
                            implicitHeight: 18
                            implicitWidth: notTestedText.implicitWidth + Theme.spacingSmall
                            Text {
                                id: notTestedText
                                anchors.centerIn: parent
                                text: qsTr("Not tested")
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                            }
                        }

                        Rectangle {
                            visible: serviceCard.capabilitiesKnown && serviceCard.supportsJsonSchema
                            color: Theme.surface
                            border.color: Theme.divider
                            border.width: 1
                            radius: Theme.radiusSmall
                            implicitHeight: 18
                            implicitWidth: schemaText.implicitWidth + Theme.spacingSmall
                            Text {
                                id: schemaText
                                anchors.centerIn: parent
                                text: "JSON Schema"
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.text
                            }
                        }

                        Rectangle {
                            visible: serviceCard.capabilitiesKnown && serviceCard.supportsTools
                            color: Theme.surface
                            border.color: Theme.divider
                            border.width: 1
                            radius: Theme.radiusSmall
                            implicitHeight: 18
                            implicitWidth: toolsText.implicitWidth + Theme.spacingSmall
                            Text {
                                id: toolsText
                                anchors.centerIn: parent
                                text: "Tools"
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.text
                            }
                        }

                        Rectangle {
                            visible: serviceCard.capabilitiesKnown && serviceCard.supportsJsonObject
                            color: Theme.surface
                            border.color: Theme.divider
                            border.width: 1
                            radius: Theme.radiusSmall
                            implicitHeight: 18
                            implicitWidth: jsonModeText.implicitWidth + Theme.spacingSmall
                            Text {
                                id: jsonModeText
                                anchors.centerIn: parent
                                text: "JSON mode"
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.text
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        // Status / Buttons
                        Label {
                            visible: serviceCard.isTesting
                            text: qsTr("Testing…")
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.accent
                        }

                        Controls.AppButton {
                            text: qsTr("Test")
                            enabled: !serviceCard.anyTesting
                            onClicked: {
                                if (AppContext.aiSettings) {
                                    AppContext.aiSettings.testService(serviceCard.serviceId)
                                }
                            }
                        }

                        Controls.AppButton {
                            text: qsTr("Set as default")
                            visible: !serviceCard.isDefault
                            enabled: !serviceCard.anyTesting
                            onClicked: {
                                if (AppContext.aiSettings) {
                                    AppContext.aiSettings.setDefaultService(serviceCard.serviceId)
                                }
                            }
                        }

                        Controls.AppButton {
                            text: qsTr("Edit")
                            enabled: !serviceCard.anyTesting
                            onClicked: {
                                serviceDialog.openEdit(
                                    serviceCard.serviceId,
                                    serviceCard.name,
                                    serviceCard.baseUrl,
                                    serviceCard.defaultModel,
                                    serviceCard.timeoutMs,
                                    serviceCard.maxConcurrent,
                                    serviceCard.requestsPerMinute
                                )
                            }
                        }

                        Controls.AppButton {
                            text: qsTr("Remove")
                            enabled: !serviceCard.anyTesting
                            onClicked: {
                                root.removeTargetId = serviceCard.serviceId
                                root.removeTargetName = serviceCard.name
                                confirmRemoveDialog.open()
                            }
                        }
                    }

                    // Second row: Base URL & default model
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingMedium

                        Label {
                            text: serviceCard.baseUrl
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Label {
                            text: qsTr("Model: %1").arg(serviceCard.defaultModel)
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                        }
                    }

                    // Third row: Test result if available
                    Label {
                        visible: serviceCard.testResult !== undefined
                        text: {
                            if (!serviceCard.testResult) return ""
                            if (serviceCard.testResult.latencyMs > 0) {
                                return serviceCard.testResult.message + " (" + serviceCard.testResult.latencyMs + " ms)"
                            }
                            return serviceCard.testResult.message
                        }
                        font.pixelSize: Theme.fontSizeSmall
                        color: serviceCard.testResult && serviceCard.testResult.ok ? Theme.accent : Theme.errorText
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Label {
            visible: servicesRepeater.count === 0
            text: qsTr("No AI services configured yet. Add a service to enable AI features.")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingTiny
            Layout.bottomMargin: Theme.spacingTiny
        }
    }

    // Action button: Add service
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Controls.AppButton {
            primary: true
            text: qsTr("Add service")
            onClicked: serviceDialog.openNew()
        }

        Item {
            Layout.fillWidth: true
        }
    }
}
