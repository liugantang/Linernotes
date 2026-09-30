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

    readonly property bool canDelete: AppContext.review && AppContext.review.currentBatchId > 0

    readonly property int currentBatchId: AppContext.review ? AppContext.review.currentBatchId : 0

    readonly property bool isWritebackRunning: AppContext.writeback ? AppContext.writeback.running : false

    readonly property int writableFileCount: {
        const _r = isWritebackRunning
        const _c = batchModelCount
        if (!AppContext.writeback || currentBatchId <= 0) return 0
        return AppContext.writeback.writableFileCount(currentBatchId)
    }

    readonly property bool hasActiveWriteback: {
        const _r = isWritebackRunning
        const _c = batchModelCount
        if (!AppContext.writeback || currentBatchId <= 0) return false
        return AppContext.writeback.hasActiveWriteback(currentBatchId)
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

        Controls.AppButton {
            text: qsTr("Delete Batch")
            enabled: root.canDelete
            onClicked: deleteConfirmDialog.open()
        }

        Controls.AppButton {
            text: qsTr("Write to Files")
            enabled: !root.isWritebackRunning && root.writableFileCount > 0
            onClicked: writebackConfirmDialog.open()
        }

        Controls.AppButton {
            text: qsTr("Undo Write")
            visible: root.hasActiveWriteback
            enabled: !root.isWritebackRunning && root.hasActiveWriteback
            onClicked: undoWriteConfirmDialog.open()
        }

        Label {
            text: qsTr("Writing... %1 / %2").arg(AppContext.writeback ? AppContext.writeback.done : 0).arg(AppContext.writeback ? AppContext.writeback.total : 0)
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            visible: root.isWritebackRunning
        }

        Label {
            text: AppContext.writeback ? AppContext.writeback.summary : ""
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            visible: !root.isWritebackRunning && text !== ""
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

    Popup {
        id: deleteConfirmDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        implicitWidth: 380
        implicitHeight: deleteDialogLayout.implicitHeight + topPadding + bottomPadding
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
            id: deleteDialogLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Delete Batch")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Remove this batch from the list? Accepted changes stay in effect but can no longer be reverted as a batch. Pending proposals will be discarded.")
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
                    onClicked: deleteConfirmDialog.close()
                }

                Controls.AppButton {
                    text: qsTr("Delete")
                    primary: true
                    onClicked: {
                        deleteConfirmDialog.close()
                        if (AppContext.review) {
                            AppContext.review.deleteBatch(AppContext.review.currentBatchId)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: writebackConfirmDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        implicitWidth: 380
        implicitHeight: writebackDialogLayout.implicitHeight + topPadding + bottomPadding
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
            id: writebackDialogLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Write to Files")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Write the accepted changes of this batch into %n file(s)? Original tags are backed up and can be restored.", "", root.writableFileCount)
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
                    onClicked: writebackConfirmDialog.close()
                }

                Controls.AppButton {
                    text: qsTr("Write to Files")
                    primary: true
                    onClicked: {
                        writebackConfirmDialog.close()
                        if (AppContext.writeback) {
                            AppContext.writeback.startWriteback(root.currentBatchId)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: undoWriteConfirmDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        implicitWidth: 380
        implicitHeight: undoDialogLayout.implicitHeight + topPadding + bottomPadding
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
            id: undoDialogLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Undo Write")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Restore the original tags of the files written by this batch?")
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
                    onClicked: undoWriteConfirmDialog.close()
                }

                Controls.AppButton {
                    text: qsTr("Undo Write")
                    primary: true
                    onClicked: {
                        undoWriteConfirmDialog.close()
                        if (AppContext.writeback) {
                            AppContext.writeback.startRevert(root.currentBatchId)
                        }
                    }
                }
            }
        }
    }
}
