// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Rectangle {
    id: root

    property bool collapsed: false

    readonly property bool hasCleanup: typeof AppContext !== "undefined" && AppContext && AppContext.cleanup
    readonly property bool hasSelectedTasks: mojibakeCheck.checked || splitCheck.checked || mergeCheck.checked

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1
    radius: Theme.radiusMedium
    clip: true

    implicitHeight: mainLayout.implicitHeight + Theme.spacingMedium * 2

    function updateAutoAcceptIndex() {
        if (!hasCleanup) return
        const val = AppContext.cleanup.autoAcceptThreshold
        if (Math.abs(val - 0.95) < 0.01) {
            autoAcceptCombo.currentIndex = 1
        } else if (Math.abs(val - 0.90) < 0.01) {
            autoAcceptCombo.currentIndex = 2
        } else {
            autoAcceptCombo.currentIndex = 0
        }
    }

    function stepTitle(step) {
        if (hasCleanup && AppContext.cleanup.paused) return qsTr("Paused")
        if (step === CleanupController.Mojibake) return qsTr("Fixing garbled tags...")
        if (step === CleanupController.Split) return qsTr("Splitting multi-artist credits...")
        if (step === CleanupController.Merge) return qsTr("Merging duplicate artists...")
        return qsTr("Cleaning up library...")
    }

    Component.onCompleted: {
        updateAutoAcceptIndex()
        if (hasCleanup && AppContext.cleanup.healthReady) {
            mojibakeCheck.checked = AppContext.cleanup.mojibakeGroups > 0
            splitCheck.checked = AppContext.cleanup.splitValues > 0
            mergeCheck.checked = AppContext.cleanup.mergeClusters > 0
        }
    }

    Connections {
        target: hasCleanup ? AppContext.cleanup : null
        function onHealthChanged() {
            if (hasCleanup && AppContext.cleanup.healthReady) {
                mojibakeCheck.checked = AppContext.cleanup.mojibakeGroups > 0
                splitCheck.checked = AppContext.cleanup.splitValues > 0
                mergeCheck.checked = AppContext.cleanup.mergeClusters > 0
            }
        }
        function onAutoAcceptThresholdChanged() {
            updateAutoAcceptIndex()
        }
    }

    ColumnLayout {
        id: mainLayout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            IconImage {
                source: "icons/sparkles.svg"
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                sourceSize: Qt.size(20, 20)
                color: Theme.accent
            }

            Label {
                text: qsTr("Library Health & Cleanup")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            Label {
                text: qsTr("Checking...")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                visible: hasCleanup && AppContext.cleanup.checking
            }

            Item { Layout.fillWidth: true }

            Controls.AppButton {
                text: qsTr("Check again")
                icon.source: "icons/rotate-ccw.svg"
                enabled: hasCleanup && !AppContext.cleanup.checking && !AppContext.cleanup.running
                onClicked: if (hasCleanup) AppContext.cleanup.checkHealth()
            }

            Controls.AppButton {
                text: root.collapsed ? qsTr("Show") : qsTr("Hide")
                onClicked: root.collapsed = !root.collapsed
            }
        }

        // Collapsible Content
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: !root.collapsed

            // AI Notice if not configured
            Rectangle {
                Layout.fillWidth: true
                visible: hasCleanup && !AppContext.cleanup.llmConfigured
                color: Theme.surfaceVariant
                border.color: Theme.divider
                border.width: 1
                radius: Theme.radiusMedium
                implicitHeight: noticeText.implicitHeight + Theme.spacingMedium * 2

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    Label {
                        id: noticeText
                        text: qsTr("No AI service is set up for cleanup. Configure one in Settings to run these tasks.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Task 1: Mojibake
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Controls.AppCheckBox {
                    id: mojibakeCheck
                    text: qsTr("Fix garbled tags")
                    enabled: hasCleanup && !AppContext.cleanup.running
                }
                Label {
                    text: hasCleanup ? (qsTr("%n folder(s) with garbled tags", "", AppContext.cleanup.mojibakeGroups)
                        + (AppContext.cleanup.mojibakeTokens > 0 ? qsTr(" · ≈ %1 tokens").arg(AppContext.cleanup.mojibakeTokens) : "")) : ""
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                }
                Item { Layout.fillWidth: true }
            }

            // Task 2: Split
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Controls.AppCheckBox {
                    id: splitCheck
                    text: qsTr("Split multi-artist credits")
                    enabled: hasCleanup && !AppContext.cleanup.running
                }
                Label {
                    text: hasCleanup ? (qsTr("%n multi-artist value(s)", "", AppContext.cleanup.splitValues)
                        + (AppContext.cleanup.splitTokens > 0 ? qsTr(" · ≈ %1 tokens").arg(AppContext.cleanup.splitTokens) : "")) : ""
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                }
                Item { Layout.fillWidth: true }
            }

            // Task 3: Merge
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTiny

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    Controls.AppCheckBox {
                        id: mergeCheck
                        text: qsTr("Merge duplicate artists")
                        enabled: hasCleanup && !AppContext.cleanup.running
                    }
                    Label {
                        text: hasCleanup ? (qsTr("%n possible duplicate artist(s)", "", AppContext.cleanup.mergeClusters)
                            + (AppContext.cleanup.mergeTokens > 0 ? qsTr(" · ≈ %1 tokens").arg(AppContext.cleanup.mergeTokens) : "")) : ""
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 28
                    Controls.AppCheckBox {
                        id: mbCheck
                        text: qsTr("Look up MusicBrainz for CJK names")
                        checked: hasCleanup ? AppContext.cleanup.useMusicBrainz : true
                        enabled: mergeCheck.checked && hasCleanup && !AppContext.cleanup.running
                        onToggled: if (hasCleanup) AppContext.cleanup.useMusicBrainz = checked
                    }
                }
            }

            // Task 4: Missing album metadata
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Label {
                    text: hasCleanup ? qsTr("Missing album metadata: %n track(s) (online lookup comes later)", "", AppContext.cleanup.missingAlbumTracks) : ""
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            // Action & Settings Bar
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                // Auto-accept dropdown
                RowLayout {
                    spacing: Theme.spacingSmall
                    visible: hasCleanup && !AppContext.cleanup.running
                    Label {
                        text: qsTr("Auto-accept:")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                    }
                    Controls.AppComboBox {
                        id: autoAcceptCombo
                        Layout.preferredWidth: 100
                        model: [qsTr("Off"), qsTr("≥ 0.95"), qsTr("≥ 0.90")]
                        onActivated: (index) => {
                            if (!hasCleanup) return
                            if (index === 1) AppContext.cleanup.autoAcceptThreshold = 0.95
                            else if (index === 2) AppContext.cleanup.autoAcceptThreshold = 0.90
                            else AppContext.cleanup.autoAcceptThreshold = 0.0
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Run button
                Controls.AppButton {
                    primary: true
                    text: qsTr("Run")
                    visible: hasCleanup && !AppContext.cleanup.running
                    enabled: root.hasSelectedTasks && hasCleanup && AppContext.cleanup.llmConfigured && !AppContext.cleanup.checking
                    onClicked: if (hasCleanup) AppContext.cleanup.run(mojibakeCheck.checked, splitCheck.checked, mergeCheck.checked, mbCheck.checked)
                }

                // Progress controls
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    visible: hasCleanup && AppContext.cleanup.running

                    Label {
                        text: hasCleanup ? root.stepTitle(AppContext.cleanup.currentStep) : ""
                        font.pixelSize: Theme.fontSizeNormal
                        font.bold: true
                        color: Theme.text
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 8
                        color: Theme.divider
                        radius: 4
                        Rectangle {
                            height: parent.height
                            radius: 4
                            color: Theme.accent
                            width: (hasCleanup && AppContext.cleanup.stepTotal > 0)
                                ? (AppContext.cleanup.stepDone / AppContext.cleanup.stepTotal) * parent.width
                                : 0
                        }
                    }

                    Label {
                        text: hasCleanup ? qsTr("%1 / %2").arg(AppContext.cleanup.stepDone).arg(AppContext.cleanup.stepTotal) : ""
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                    }

                    Label {
                        text: hasCleanup ? qsTr("(%1 failed)").arg(AppContext.cleanup.stepFailed) : ""
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.errorText
                        visible: hasCleanup && AppContext.cleanup.stepFailed > 0
                    }

                    Controls.AppButton {
                        text: (hasCleanup && AppContext.cleanup.paused) ? qsTr("Resume") : qsTr("Pause")
                        onClicked: if (hasCleanup) (AppContext.cleanup.paused ? AppContext.cleanup.resume() : AppContext.cleanup.pause())
                    }

                    Controls.AppButton {
                        text: qsTr("Cancel")
                        onClicked: if (hasCleanup) AppContext.cleanup.cancel()
                    }
                }
            }
        }
    }
}
