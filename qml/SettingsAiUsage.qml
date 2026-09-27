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

    property string statusMessage: ""
    property bool statusOk: true

    function formatNumber(val) {
        if (val === undefined || val === null) return "0"
        return Number(val).toLocaleString(Qt.locale(), 'f', 0)
    }

    function purposeDisplayName(p) {
        if (p === Ai.Cleanup) return qsTr("Metadata cleanup")
        if (p === Ai.Query) return qsTr("Natural-language search")
        if (p === Ai.Dj) return qsTr("AI DJ")
        if (p === Ai.Guide) return qsTr("Listening guide")
        if (p === Ai.Narrative) return qsTr("Liner notes & narrative")
        return qsTr("Unknown")
    }

    Component.onCompleted: {
        if (AppContext.aiSettings) {
            AppContext.aiSettings.refreshUsage(30)
        }
    }

    onVisibleChanged: {
        if (visible && AppContext.aiSettings) {
            AppContext.aiSettings.refreshUsage(rangeCombo.currentValue)
        }
    }

    Connections {
        target: AppContext.aiSettings
        function onCacheCleared(ok) {
            root.statusOk = ok
            root.statusMessage = ok
                ? qsTr("Response cache cleared successfully.")
                : qsTr("Failed to clear response cache.")
            if (ok && AppContext.aiSettings) {
                AppContext.aiSettings.refreshUsage(rangeCombo.currentValue)
            }
        }
    }

    // Header row: Section Title, Time Range, Clear Cache button
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        Label {
            text: qsTr("Usage")
            font.pixelSize: Theme.fontSizeNormal
            font.bold: true
            color: Theme.text
        }

        Item {
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Time range:")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
        }

        Controls.AppComboBox {
            id: rangeCombo
            Layout.preferredWidth: 120
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("7 days"), value: 7 },
                { text: qsTr("30 days"), value: 30 },
                { text: qsTr("90 days"), value: 90 }
            ]
            currentIndex: 1
            onActivated: {
                if (AppContext.aiSettings) {
                    AppContext.aiSettings.refreshUsage(currentValue)
                }
            }
        }

        Controls.AppButton {
            text: qsTr("Clear response cache")
            onClicked: {
                if (AppContext.aiSettings) {
                    AppContext.aiSettings.clearCache()
                }
            }
        }
    }

    // Status message for cache clear
    Label {
        visible: root.statusMessage.length > 0
        text: root.statusMessage
        font.pixelSize: Theme.fontSizeSmall
        color: root.statusOk ? Theme.accent : Theme.errorText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    // Totals line
    Label {
        text: qsTr("Total: %1 requests, %2 input tokens, %3 output tokens").arg(
            root.formatNumber(AppContext.aiSettings ? AppContext.aiSettings.usage.totalRequests : 0)).arg(
            root.formatNumber(AppContext.aiSettings ? AppContext.aiSettings.usage.totalPromptTokens : 0)).arg(
            root.formatNumber(AppContext.aiSettings ? AppContext.aiSettings.usage.totalCompletionTokens : 0))
        font.pixelSize: Theme.fontSizeNormal
        color: Theme.text
        Layout.fillWidth: true
    }

    // Usage table container
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: tableLayout.implicitHeight
        color: Theme.surfaceVariant
        radius: Theme.radiusMedium
        border.color: Theme.divider
        border.width: 1
        clip: true

        ColumnLayout {
            id: tableLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0

            // Table header
            Rectangle {
                Layout.fillWidth: true
                height: Theme.tableRowHeight
                color: Theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    spacing: Theme.spacingSmall

                    Label {
                        text: qsTr("Feature")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 150
                        elide: Text.ElideRight
                    }

                    Label {
                        text: qsTr("Model")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    Label {
                        text: qsTr("Requests")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 65
                        horizontalAlignment: Text.AlignRight
                    }

                    Label {
                        text: qsTr("Cache Hits")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 75
                        horizontalAlignment: Text.AlignRight
                    }

                    Label {
                        text: qsTr("Failures")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignRight
                    }

                    Label {
                        text: qsTr("Input Tokens")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 90
                        horizontalAlignment: Text.AlignRight
                    }

                    Label {
                        text: qsTr("Output Tokens")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 95
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.divider
                }
            }

            // Table rows
            Repeater {
                id: usageRepeater
                model: AppContext.aiSettings ? AppContext.aiSettings.usage : null

                delegate: Item {
                    id: rowItem
                    Layout.fillWidth: true
                    implicitHeight: Theme.tableRowHeight

                    required property var purpose
                    required property string model
                    required property int requests
                    required property int cacheHits
                    required property int failures
                    required property int promptTokens
                    required property int completionTokens
                    required property int index

                    Rectangle {
                        anchors.fill: parent
                        color: rowItem.index % 2 === 1 ? Theme.hoverOverlay : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        Label {
                            text: root.purposeDisplayName(rowItem.purpose)
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.text
                            Layout.preferredWidth: 150
                            elide: Text.ElideRight
                        }

                        Label {
                            text: rowItem.model
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Label {
                            text: root.formatNumber(rowItem.requests)
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.text
                            Layout.preferredWidth: 65
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: root.formatNumber(rowItem.cacheHits)
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            Layout.preferredWidth: 75
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: root.formatNumber(rowItem.failures)
                            font.pixelSize: Theme.fontSizeNormal
                            color: rowItem.failures > 0 ? Theme.errorText : Theme.textSecondary
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: root.formatNumber(rowItem.promptTokens)
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.text
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: root.formatNumber(rowItem.completionTokens)
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.text
                            Layout.preferredWidth: 95
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.divider
                    }
                }
            }

            // Empty state
            Label {
                visible: !AppContext.aiSettings || !AppContext.aiSettings.usage || AppContext.aiSettings.usage.rowCount() === 0
                text: qsTr("No usage yet.")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.fillWidth: true
                Layout.margins: Theme.spacingMedium
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
