// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes
import "controls" as Controls
import "TrackColumns.js" as TrackColumns

Rectangle {
    id: root

    required property var table
    required property var visibleColumns
    required property real contentOffsetX
    required property real totalWidth

    height: Theme.tableRowHeight
    color: Theme.surface
    z: 2

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.divider
    }

    Item {
        id: headerRowContainer
        x: -root.contentOffsetX
        width: root.totalWidth
        height: parent.height

        Row {
            anchors.fill: parent
            spacing: 0

            Repeater {
                model: root.visibleColumns

                delegate: Item {
                    id: colHeaderItem
                    required property var modelData
                    required property int index

                    readonly property real colWidth: root.table.getColumnWidth(modelData.key)
                    readonly property bool isSortColumn: root.table.model.sortKey === modelData.sortKey && modelData.sortKey !== -1

                    width: colWidth
                    height: root.height

                    Rectangle {
                        id: headerHoverBg
                        anchors.fill: parent
                        color: headerMouseArea.containsMouse ? Theme.hoverOverlay : "transparent"
                    }

                    IconImage {
                        visible: modelData.key === "favorite"
                        anchors.centerIn: parent
                        source: "icons/heart.svg"
                        sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
                        color: Theme.textSecondary
                        width: Theme.iconSize
                        height: Theme.iconSize
                    }

                    RowLayout {
                        visible: modelData.key !== "favorite"
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingTiny

                        Label {
                            text: root.table.columnTitle(modelData.key)
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                            color: colHeaderItem.isSortColumn ? Theme.accent : Theme.textSecondary
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: modelData.alignRight ? Text.AlignRight : Text.AlignLeft
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Image {
                            visible: colHeaderItem.isSortColumn
                            source: root.table.model.sortOrder === Qt.AscendingOrder
                                ? "icons/arrow-up-narrow-wide.svg"
                                : "icons/arrow-down-wide-narrow.svg"
                            sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
                            Layout.preferredWidth: Theme.iconSize
                            Layout.preferredHeight: Theme.iconSize
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: Theme.spacingTiny
                        anchors.bottomMargin: Theme.spacingTiny
                        width: 1
                        color: Theme.divider
                    }

                    MouseArea {
                        id: headerMouseArea
                        anchors.fill: parent
                        anchors.rightMargin: 4
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: true

                        onClicked: (mouse) => {
                            if (mouse.button === Qt.LeftButton) {
                                if (modelData.sortKey !== -1) {
                                    root.table.handleSort(modelData.key, modelData.sortKey)
                                }
                            } else if (mouse.button === Qt.RightButton) {
                                headerContextMenu.popup()
                            }
                        }
                    }

                    MouseArea {
                        id: resizeHandle
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 8
                        cursorShape: Qt.SplitHCursor
                        hoverEnabled: true

                        property real startX: 0
                        property real startWidth: 0

                        onPressed: (mouse) => {
                            startX = mouse.x
                            startWidth = colHeaderItem.colWidth
                        }

                        onPositionChanged: (mouse) => {
                            if (pressed) {
                                const delta = mouse.x - startX
                                root.table.setColumnWidth(modelData.key, startWidth + delta)
                            }
                        }
                    }
                }
            }
        }
    }

    Controls.AppMenu {
        id: headerContextMenu

        Repeater {
            model: TrackColumns.ALL_COLUMNS

            delegate: Controls.AppMenuItem {
                required property var modelData
                text: modelData.key === "favorite" ? qsTr("Loved") : root.table.columnTitle(modelData.key)
                checkable: true
                checked: root.table.isColumnVisible(modelData.key)
                enabled: modelData.canHide
                onTriggered: {
                    root.table.toggleColumnVisibility(modelData.key)
                }
            }
        }

        Controls.AppMenuSeparator {}

        Controls.AppMenuItem {
            text: qsTr("Restore Default Columns")
            onTriggered: {
                root.table.restoreDefaultColumns()
            }
        }
    }
}
