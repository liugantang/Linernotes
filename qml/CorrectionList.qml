// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Rectangle {
    id: root

    color: Theme.background

    readonly property bool hasItems: AppContext.review && AppContext.review.listModel && AppContext.review.listModel.count > 0
    readonly property bool isCompact: width < 560

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Table header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                Controls.AppCheckBox {
                    id: headerCheckBox
                    checked: AppContext.review && AppContext.review.listModel
                        && AppContext.review.listModel.selection
                        && AppContext.review.listModel.count > 0
                        && AppContext.review.listModel.selection.count === AppContext.review.listModel.count
                    Layout.alignment: Qt.AlignVCenter

                    onClicked: {
                        if (AppContext.review && AppContext.review.listModel) {
                            if (checked) {
                                AppContext.review.listModel.selectAll()
                            } else {
                                AppContext.review.listModel.clearSelection()
                            }
                        }
                    }
                }

                Label {
                    text: qsTr("Correction / Field / Change")
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                RowLayout {
                    spacing: root.isCompact ? Theme.spacingSmall : Theme.spacingMedium
                    Layout.alignment: Qt.AlignVCenter

                    Label {
                        text: qsTr("Confidence")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: root.isCompact ? 44 : 64
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Label {
                        text: qsTr("Source")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        visible: !root.isCompact
                    }

                    Label {
                        text: qsTr("Status")
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.preferredWidth: root.isCompact ? 68 : 76
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Empty state
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.hasItems

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("No corrections match the current filter.")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // List view
        ListView {
            id: correctionListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: AppContext.review ? AppContext.review.listModel : null
            boundsBehavior: Flickable.StopAtBounds
            visible: root.hasItems

            ScrollBar.vertical: Controls.AppScrollBar {}

            delegate: CorrectionRowItem {
                width: correctionListView.width
            }
        }
    }
}
