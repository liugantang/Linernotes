// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    readonly property bool hasBatches: AppContext.review && AppContext.review.batchModel && AppContext.review.batchModel.count > 0

    Component.onCompleted: {
        if (AppContext.review) {
            AppContext.review.refresh()
            if (AppContext.review.currentBatchId <= 0 && AppContext.review.batchModel.count > 0) {
                AppContext.review.selectBatch(AppContext.review.batchModel.batchIdAt(0))
            }
        }
    }

    Connections {
        target: AppContext.review ? AppContext.review.batchModel : null
        function onCountChanged() {
            if (AppContext.review && AppContext.review.currentBatchId <= 0 && AppContext.review.batchModel.count > 0) {
                AppContext.review.selectBatch(AppContext.review.batchModel.batchIdAt(0))
            }
        }
    }

    Rectangle {
        id: errorBanner
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: AppContext.review && AppContext.review.errorText.length > 0
        height: visible ? (errorTextLabel.implicitHeight + Theme.spacingMedium * 2) : 0
        color: Theme.errorBackground
        border.color: Theme.errorBorder
        border.width: 1
        z: 10

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Label {
                id: errorTextLabel
                text: AppContext.review ? AppContext.review.errorText : ""
                color: Theme.errorText
                font.pixelSize: Theme.fontSizeNormal
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    Item {
        anchors.top: errorBanner.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !root.hasBatches

        ColumnLayout {
            anchors.centerIn: parent
            spacing: Theme.spacingMedium
            width: Math.min(parent.width - Theme.spacingLarge * 2, 400)

            IconImage {
                source: "../icons/sparkles.svg"
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                sourceSize: Qt.size(48, 48)
                color: Theme.textSecondary
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("No Cleanup Batches")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("No cleanup batches yet. Run a library cleanup to see proposed fixes here.")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }
        }
    }

    RowLayout {
        anchors.top: errorBanner.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0
        visible: root.hasBatches

        CorrectionBatchList {
            Layout.preferredWidth: root.width < 900 ? 220 : 280
            Layout.fillHeight: true
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.divider
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            CorrectionFilterBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            CorrectionList {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            CorrectionActionBar {
                Layout.fillWidth: true
            }
        }
    }
}
