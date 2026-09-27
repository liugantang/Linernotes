// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Linernotes
import "controls" as Controls

Window {
    id: root
    title: qsTr("LLM Debug")
    width: 1000
    height: 640
    minimumWidth: 700
    minimumHeight: 400
    color: Theme.background

    property var selectedEntryId: 0

    function purposeDisplayName(p) {
        if (p === Ai.Cleanup) return qsTr("Metadata cleanup")
        if (p === Ai.Query) return qsTr("Natural-language search")
        if (p === Ai.Dj) return qsTr("AI DJ")
        if (p === Ai.Guide) return qsTr("Listening guide")
        if (p === Ai.Narrative) return qsTr("Liner notes & narrative")
        return qsTr("Unknown")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Top Header Bar: Title, Replay & Clear buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("LLM Debug Log")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Replay")
                enabled: root.selectedEntryId > 0 && AppContext.llmDebug !== null
                onClicked: {
                    if (AppContext.llmDebug && root.selectedEntryId > 0) {
                        AppContext.llmDebug.replay(root.selectedEntryId)
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Clear")
                enabled: AppContext.llmDebug !== null
                onClicked: {
                    if (AppContext.llmDebug) {
                        AppContext.llmDebug.clear()
                        root.selectedEntryId = 0
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Main content: Left list + Right details
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingMedium

            // Left side: Entries list
            Rectangle {
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                color: Theme.surface
                radius: Theme.radiusMedium
                border.color: Theme.divider
                border.width: 1
                clip: true

                ListView {
                    id: entriesList
                    anchors.fill: parent
                    anchors.margins: 1
                    model: AppContext.llmDebug ? AppContext.llmDebug.entries : null
                    clip: true
                    ScrollBar.vertical: Controls.AppScrollBar {}

                    delegate: Item {
                        id: delegateItem
                        width: entriesList.width
                        height: 60

                        required property var entryId
                        required property string time
                        required property var purpose
                        required property string model
                        required property int attempt
                        required property bool fromCache
                        required property bool ok
                        required property int httpStatus
                        required property string errorCode
                        required property int elapsedMs
                        required property int promptTokens
                        required property int completionTokens
                        required property int index

                        readonly property bool isSelected: root.selectedEntryId === delegateItem.entryId

                        Rectangle {
                            anchors.fill: parent
                            color: delegateItem.isSelected
                                ? Theme.itemSelected
                                : (itemMouseArea.containsMouse ? Theme.itemHover : "transparent")
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingSmall
                            anchors.rightMargin: Theme.spacingSmall
                            anchors.topMargin: Theme.spacingTiny
                            anchors.bottomMargin: Theme.spacingTiny
                            spacing: 2

                            // Line 1: Time, Purpose, Status, Cached tag
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Label {
                                    text: delegateItem.time
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.textSecondary
                                }

                                Label {
                                    text: root.purposeDisplayName(delegateItem.purpose)
                                    font.pixelSize: Theme.fontSizeSmall
                                    font.bold: true
                                    color: Theme.text
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    visible: delegateItem.fromCache
                                    height: 18
                                    width: cachedLabel.implicitWidth + 8
                                    radius: Theme.radiusSmall
                                    color: Theme.surfaceVariant

                                    Label {
                                        id: cachedLabel
                                        anchors.centerIn: parent
                                        text: qsTr("cached")
                                        font.pixelSize: Theme.fontSizeSmall - 2
                                        color: Theme.accent
                                    }
                                }

                                Rectangle {
                                    height: 18
                                    width: statusLabel.implicitWidth + 8
                                    radius: Theme.radiusSmall
                                    color: delegateItem.ok ? Qt.rgba(0, 0.7, 0.2, 0.15) : Qt.rgba(0.9, 0.2, 0.2, 0.15)

                                    Label {
                                        id: statusLabel
                                        anchors.centerIn: parent
                                        text: delegateItem.ok
                                            ? (delegateItem.httpStatus > 0 ? "" + delegateItem.httpStatus : "OK")
                                            : (delegateItem.errorCode.length > 0 ? delegateItem.errorCode : "" + delegateItem.httpStatus)
                                        font.pixelSize: Theme.fontSizeSmall - 2
                                        font.bold: true
                                        color: delegateItem.ok ? (Theme.isDark ? "#a6e3a1" : "#2e7d32") : Theme.errorText
                                    }
                                }
                            }

                            // Line 2: Model, Attempt, Elapsed ms, Tokens
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Label {
                                    text: delegateItem.model
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.textSecondary
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Label {
                                    visible: delegateItem.attempt > 1
                                    text: qsTr("att %1").arg(delegateItem.attempt)
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.textSecondary
                                }

                                Label {
                                    text: qsTr("%1 ms").arg(delegateItem.elapsedMs)
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.textSecondary
                                }

                                Label {
                                    text: (delegateItem.promptTokens + delegateItem.completionTokens) > 0
                                        ? qsTr("%1 tok").arg(delegateItem.promptTokens + delegateItem.completionTokens)
                                        : ""
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.textSecondary
                                }
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.divider
                        }

                        MouseArea {
                            id: itemMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.selectedEntryId = delegateItem.entryId
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: entriesList.count === 0
                        text: qsTr("No LLM requests recorded yet.")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                    }
                }
            }

            // Right side: Details pane (Request / Response / Error)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                radius: Theme.radiusMedium
                border.color: Theme.divider
                border.width: 1
                clip: true

                Label {
                    anchors.centerIn: parent
                    visible: root.selectedEntryId <= 0
                    text: qsTr("Select a request from the list to view details.")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                }

                ScrollView {
                    id: detailsScroll
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    visible: root.selectedEntryId > 0
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.vertical: Controls.AppScrollBar {}

                    ColumnLayout {
                        width: detailsScroll.availableWidth
                        spacing: Theme.spacingMedium

                        // 1. Request section
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: qsTr("Request (JSON)")
                                    font.pixelSize: Theme.fontSizeNormal
                                    font.bold: true
                                    color: Theme.text
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Controls.AppButton {
                                    text: qsTr("Copy")
                                    onClicked: {
                                        if (AppContext.llmDebug) {
                                            AppContext.llmDebug.copyToClipboard(requestArea.text)
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: Math.max(80, requestArea.contentHeight + Theme.spacingSmall * 2)
                                color: Theme.surfaceVariant
                                radius: Theme.radiusMedium
                                border.color: Theme.divider
                                border.width: 1

                                TextArea {
                                    id: requestArea
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    font.family: "monospace"
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.text
                                    selectedTextColor: Theme.accentText
                                    selectionColor: Theme.accent
                                    background: null
                                    text: AppContext.llmDebug ? AppContext.llmDebug.requestText(root.selectedEntryId) : ""
                                }
                            }
                        }

                        // 2. Response section
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: qsTr("Response")
                                    font.pixelSize: Theme.fontSizeNormal
                                    font.bold: true
                                    color: Theme.text
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Controls.AppButton {
                                    text: qsTr("Copy")
                                    onClicked: {
                                        if (AppContext.llmDebug) {
                                            AppContext.llmDebug.copyToClipboard(responseArea.text)
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: Math.max(80, responseArea.contentHeight + Theme.spacingSmall * 2)
                                color: Theme.surfaceVariant
                                radius: Theme.radiusMedium
                                border.color: Theme.divider
                                border.width: 1

                                TextArea {
                                    id: responseArea
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    font.family: "monospace"
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.text
                                    selectedTextColor: Theme.accentText
                                    selectionColor: Theme.accent
                                    background: null
                                    text: AppContext.llmDebug ? AppContext.llmDebug.responseText(root.selectedEntryId) : ""
                                }
                            }
                        }

                        // 3. Error section
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall
                            visible: errorArea.text.length > 0

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: qsTr("Error")
                                    font.pixelSize: Theme.fontSizeNormal
                                    font.bold: true
                                    color: Theme.errorText
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Controls.AppButton {
                                    text: qsTr("Copy")
                                    onClicked: {
                                        if (AppContext.llmDebug) {
                                            AppContext.llmDebug.copyToClipboard(errorArea.text)
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: Math.max(60, errorArea.contentHeight + Theme.spacingSmall * 2)
                                color: Theme.errorBackground
                                radius: Theme.radiusMedium
                                border.color: Theme.errorBorder
                                border.width: 1

                                TextArea {
                                    id: errorArea
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    font.family: "monospace"
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.errorText
                                    selectedTextColor: Theme.accentText
                                    selectionColor: Theme.accent
                                    background: null
                                    text: AppContext.llmDebug ? AppContext.llmDebug.errorText(root.selectedEntryId) : ""
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
