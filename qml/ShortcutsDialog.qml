// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    property var shortcuts: null
    readonly property var entries: shortcuts ? shortcuts.entries : []

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay

    implicitWidth: 540
    implicitHeight: Math.min(620, contentColumn.implicitHeight + topPadding + bottomPadding)
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

    Shortcut {
        sequence: "Esc"
        context: Qt.WindowShortcut
        onActivated: root.close()
    }

    readonly property var groups: [
        { id: AppShortcuts.Group.Playback, title: qsTr("Playback") },
        { id: AppShortcuts.Group.Navigation, title: qsTr("Navigation") },
        { id: AppShortcuts.Group.Library, title: qsTr("Library") },
        { id: AppShortcuts.Group.General, title: qsTr("General") }
    ]

    function getEntriesForGroup(groupId) {
        return root.entries.filter(e => e.group === groupId && !e.hidden)
    }

    contentItem: ColumnLayout {
        id: contentColumn
        spacing: Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Keyboard Shortcuts")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Controls.IconButton {
                icon.source: "icons/x.svg"
                toolTip: qsTr("Close (Esc)")
                onClicked: root.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: 480
            contentWidth: availableWidth
            clip: true

            ScrollBar.vertical: Controls.AppScrollBar {}

            ColumnLayout {
                width: parent.width
                spacing: Theme.spacingMedium

                Repeater {
                    model: root.groups

                    delegate: ColumnLayout {
                        id: groupSection
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        readonly property var groupItems: root.getEntriesForGroup(modelData.id)
                        visible: groupItems.length > 0

                        Label {
                            text: groupSection.modelData.title
                            font.pixelSize: Theme.fontSizeNormal
                            font.bold: true
                            color: Theme.accent
                            Layout.fillWidth: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingTiny

                            Repeater {
                                model: groupSection.groupItems

                                delegate: Rectangle {
                                    id: rowRect
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 30
                                    radius: Theme.radiusSmall
                                    color: itemMouseArea.containsMouse ? Theme.itemHover : "transparent"

                                    MouseArea {
                                        id: itemMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: Theme.spacingSmall
                                        anchors.rightMargin: Theme.spacingSmall
                                        spacing: Theme.spacingMedium

                                        Label {
                                            text: rowRect.modelData.description
                                            font.pixelSize: Theme.fontSizeNormal
                                            color: Theme.text
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }

                                        Rectangle {
                                            implicitHeight: 22
                                            implicitWidth: keyLabel.implicitWidth + Theme.spacingSmall * 2
                                            radius: Theme.radiusSmall
                                            color: Theme.surfaceVariant
                                            border.color: Theme.divider
                                            border.width: 1

                                            Label {
                                                id: keyLabel
                                                anchors.centerIn: parent
                                                text: rowRect.modelData.sequence
                                                font.pixelSize: Theme.fontSizeSmall
                                                font.bold: true
                                                color: Theme.text
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            Layout.topMargin: Theme.spacingSmall
                            color: Theme.divider
                            visible: groupSection.modelData.id !== AppShortcuts.Group.General
                        }
                    }
                }
            }
        }
    }
}
