// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Rectangle {
    id: root

    color: Theme.surface
    implicitHeight: flowLayout.implicitHeight + Theme.spacingMedium * 2

    readonly property bool hasSelection: AppContext.review && AppContext.review.listModel
        && AppContext.review.listModel.selection
        && AppContext.review.listModel.selection.count > 0

    readonly property double currentMinConfidence: AppContext.review && AppContext.review.listModel
        ? AppContext.review.listModel.minConfidence
        : 0.0

    readonly property int pendingCount: AppContext.review && AppContext.review.listModel
        ? AppContext.review.listModel.pendingCount
        : 0

    readonly property bool hasPending: {
        if (!AppContext.review || !AppContext.review.listModel || pendingCount === 0) return false
        return AppContext.review.listModel.allPendingCorrectionIds(currentMinConfidence).length > 0
    }

    readonly property int batchModelCount: AppContext.review && AppContext.review.batchModel
        ? AppContext.review.batchModel.count
        : 0

    readonly property bool canRevert: {
        if (!AppContext.review || AppContext.review.currentBatchId <= 0 || !AppContext.review.batchModel || batchModelCount === 0) return false
        const batchId = AppContext.review.currentBatchId
        const model = AppContext.review.batchModel
        for (let i = 0; i < model.count; ++i) {
            if (model.batchIdAt(i) === batchId) {
                return !model.data(model.index(i, 0), CorrectionBatchModel.RevertedRole)
            }
        }
        return false
    }

    Flow {
        id: flowLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingSmall

        Controls.AppButton {
            text: qsTr("Accept Selected")
            primary: true
            enabled: root.hasSelection
            onClicked: {
                if (AppContext.review) {
                    AppContext.review.acceptSelected()
                }
            }
        }

        Controls.AppButton {
            text: qsTr("Reject Selected")
            enabled: root.hasSelection
            onClicked: {
                if (AppContext.review) {
                    AppContext.review.rejectSelected()
                }
            }
        }

        Controls.AppButton {
            text: qsTr("Accept All Pending (≥ %1%)").arg(Math.round(root.currentMinConfidence * 100))
            enabled: root.hasPending
            onClicked: {
                if (AppContext.review) {
                    AppContext.review.acceptAllPending(root.currentMinConfidence)
                }
            }
        }

        Controls.AppButton {
            text: qsTr("Revert Batch")
            enabled: root.canRevert
            onClicked: revertConfirmDialog.open()
        }
    }

    Popup {
        id: revertConfirmDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        implicitWidth: 380
        implicitHeight: dialogLayout.implicitHeight + topPadding + bottomPadding
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
            id: dialogLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Revert Batch")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Are you sure you want to revert this batch? All accepted changes will be reverted, and pending proposals will be rejected.")
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
                    onClicked: revertConfirmDialog.close()
                }

                Controls.AppButton {
                    text: qsTr("Revert")
                    primary: true
                    onClicked: {
                        revertConfirmDialog.close()
                        if (AppContext.review) {
                            AppContext.review.revertBatch(AppContext.review.currentBatchId)
                        }
                    }
                }
            }
        }
    }
}
