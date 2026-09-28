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
    clip: true

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: root.width < 500 ? Theme.spacingSmall : Theme.spacingMedium

        // Status filter
        RowLayout {
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Status:")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }

            Controls.AppComboBox {
                id: statusCombo
                Layout.preferredWidth: 88
                model: [
                    qsTr("All"),
                    qsTr("Pending"),
                    qsTr("Accepted"),
                    qsTr("Rejected"),
                    qsTr("Reverted"),
                    qsTr("Stale")
                ]
                currentIndex: AppContext.review && AppContext.review.listModel ? AppContext.review.listModel.statusFilter : 0
                onActivated: (index) => {
                    if (AppContext.review && AppContext.review.listModel) {
                        AppContext.review.listModel.statusFilter = index
                    }
                }
            }

            Controls.AppButton {
                visible: AppContext.review && AppContext.review.listModel && AppContext.review.listModel.staleCount > 0
                text: AppContext.review && AppContext.review.listModel
                    ? qsTr("%n stale", "", AppContext.review.listModel.staleCount)
                    : ""
                palette.buttonText: Theme.errorText
                palette.brightText: Theme.errorText
                Layout.preferredHeight: 28
                onClicked: {
                    if (AppContext.review) {
                        AppContext.review.showStale()
                    }
                }
            }
        }

        // Confidence filter
        RowLayout {
            spacing: Theme.spacingSmall
            Layout.fillWidth: true

            Label {
                text: root.width < 500
                    ? qsTr("Min: %1%").arg(Math.round(confidenceSlider.value * 100))
                    : qsTr("Min Confidence: %1%").arg(Math.round(confidenceSlider.value * 100))
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }

            Slider {
                id: confidenceSlider
                from: 0.0
                to: 1.0
                stepSize: 0.05
                value: AppContext.review && AppContext.review.listModel ? AppContext.review.listModel.minConfidence : 0.0
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                Layout.preferredWidth: 100
                Layout.maximumWidth: 160

                onMoved: {
                    if (AppContext.review && AppContext.review.listModel) {
                        AppContext.review.listModel.minConfidence = value
                    }
                }

                background: Rectangle {
                    x: confidenceSlider.leftPadding
                    y: confidenceSlider.topPadding + confidenceSlider.availableHeight / 2 - height / 2
                    implicitWidth: 100
                    implicitHeight: 4
                    width: confidenceSlider.availableWidth
                    height: implicitHeight
                    radius: 2
                    color: Theme.surfaceVariant

                    Rectangle {
                        width: confidenceSlider.visualPosition * parent.width
                        height: parent.height
                        color: Theme.accent
                        radius: 2
                    }
                }

                handle: Rectangle {
                    x: confidenceSlider.leftPadding + confidenceSlider.visualPosition * (confidenceSlider.availableWidth - width)
                    y: confidenceSlider.topPadding + confidenceSlider.availableHeight / 2 - height / 2
                    implicitWidth: 14
                    implicitHeight: 14
                    radius: 7
                    color: confidenceSlider.pressed ? Theme.accentHover : Theme.accent
                    border.color: Theme.surface
                    border.width: 1
                }
            }

            Controls.AppButton {
                text: qsTr("Reset")
                visible: Math.round(confidenceSlider.value * 100) > 0
                Layout.preferredHeight: 28
                onClicked: {
                    confidenceSlider.value = 0.0
                    if (AppContext.review && AppContext.review.listModel) {
                        AppContext.review.listModel.minConfidence = 0.0
                    }
                }
            }
        }

        // Count
        Label {
            text: AppContext.review && AppContext.review.listModel ? qsTr("%n item(s)", "", AppContext.review.listModel.count) : ""
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            elide: Text.ElideRight
            visible: root.width >= 460
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
