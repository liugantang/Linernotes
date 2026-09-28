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

    color: Theme.surface

    function kindIcon(kind) {
        if (kind === Library.CorrectionKind.Mojibake) return "rotate-ccw"
        if (kind === Library.CorrectionKind.ArtistSplit || kind === Library.CorrectionKind.ArtistCredit || kind === Library.CorrectionKind.ArtistMerge) return "mic-vocal"
        return "sparkles"
    }

    function kindTitle(kind) {
        if (kind === Library.CorrectionKind.Mojibake) return qsTr("Mojibake Fix")
        if (kind === Library.CorrectionKind.ArtistCredit) return qsTr("Artist Credits")
        if (kind === Library.CorrectionKind.ArtistSplit) return qsTr("Split Artists")
        if (kind === Library.CorrectionKind.ArtistMerge) return qsTr("Merge Artists")
        return qsTr("Manual Fix")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Batches")
                    font.pixelSize: Theme.fontSizeNormal
                    font.bold: true
                    color: Theme.text
                }

                Label {
                    text: AppContext.review && AppContext.review.batchModel ? qsTr("%n batch(es)", "", AppContext.review.batchModel.count) : ""
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Clear Processed")
                    font.pixelSize: Theme.fontSizeSmall
                    implicitHeight: 28
                    enabled: AppContext.review && AppContext.review.batchModel && AppContext.review.batchModel.hasDecided
                    onClicked: clearConfirmDialog.open()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Batch list
        ListView {
            id: batchListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AppContext.review ? AppContext.review.batchModel : null
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: Controls.AppScrollBar {}

            delegate: Rectangle {
                id: delegateRoot
                required property int index
                required property var batchId
                required property int kind
                required property string description
                required property string createdText
                required property int pendingCount
                required property int acceptedCount
                required property int rejectedCount
                required property int revertedCount
                required property bool reverted

                readonly property bool isCurrent: AppContext.review && AppContext.review.currentBatchId === batchId

                width: batchListView.width
                height: 76
                color: {
                    if (isCurrent) return Theme.itemSelected
                    if (mouseArea.containsMouse) return Theme.itemHover
                    return "transparent"
                }

                MouseArea {
                    id: mouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (AppContext.review) {
                            AppContext.review.selectBatch(delegateRoot.batchId)
                        }
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: 2
                    opacity: delegateRoot.reverted ? 0.6 : 1.0

                    // Top row: icon + title + time
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        IconImage {
                            source: "icons/" + root.kindIcon(delegateRoot.kind) + ".svg"
                            Layout.preferredWidth: Theme.iconSize
                            Layout.preferredHeight: Theme.iconSize
                            sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
                            color: delegateRoot.reverted ? Theme.textSecondary : (delegateRoot.isCurrent ? Theme.accent : Theme.text)
                        }

                        Label {
                            text: root.kindTitle(delegateRoot.kind)
                            font.pixelSize: Theme.fontSizeNormal
                            font.bold: true
                            color: delegateRoot.reverted ? Theme.textSecondary : (delegateRoot.isCurrent ? Theme.accent : Theme.text)
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Label {
                            text: delegateRoot.createdText
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: Theme.textSecondary
                        }
                    }

                    // Description
                    Label {
                        text: delegateRoot.description
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        visible: text.length > 0
                    }

                    // Status row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Rectangle {
                            visible: delegateRoot.reverted
                            color: Theme.surfaceVariant
                            radius: Theme.radiusSmall
                            Layout.preferredHeight: 18
                            Layout.preferredWidth: revertedBadgeLabel.implicitWidth + Theme.spacingSmall

                            Label {
                                id: revertedBadgeLabel
                                anchors.centerIn: parent
                                text: qsTr("Reverted")
                                font.pixelSize: Theme.fontSizeSmall - 2
                                color: Theme.textSecondary
                            }
                        }

                        Label {
                            visible: !delegateRoot.reverted
                            text: qsTr("%1 pending · %2 accepted").arg(delegateRoot.pendingCount).arg(delegateRoot.acceptedCount)
                            font.pixelSize: Theme.fontSizeSmall - 1
                            color: delegateRoot.pendingCount > 0 ? (Theme.isDark ? "#f9e2af" : "#b45309") : Theme.textSecondary
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.divider
                }
            }
        }
    }

    Popup {
        id: clearConfirmDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        implicitWidth: 380
        implicitHeight: clearDialogLayout.implicitHeight + topPadding + bottomPadding
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
            id: clearDialogLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Clear Processed")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Remove all batches with no pending proposals from the list? Accepted changes stay in effect.")
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
                    onClicked: clearConfirmDialog.close()
                }

                Controls.AppButton {
                    text: qsTr("Delete")
                    primary: true
                    onClicked: {
                        clearConfirmDialog.close()
                        if (AppContext.review) {
                            AppContext.review.deleteDecidedBatches()
                        }
                    }
                }
            }
        }
    }
}
