// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "../controls" as Controls

Item {
    id: root

    readonly property var navSections: [
        { title: qsTr("Library") },
        { title: qsTr("Playback") },
        { title: qsTr("Appearance") },
        { title: qsTr("AI") }
    ]

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spacingLarge
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        anchors.bottomMargin: 0
        spacing: Theme.spacingLarge

        // Top title
        Label {
            text: qsTr("Settings")
            font.pixelSize: Theme.fontSizeTitle
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingLarge

            // Left: Vertical navigation
            ListView {
                id: navListView
                Layout.preferredWidth: 180
                Layout.fillHeight: true
                model: root.navSections
                currentIndex: 0
                spacing: Theme.spacingTiny
                clip: true
                focus: true
                keyNavigationEnabled: true
                keyNavigationWraps: false
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: itemDelegate
                    required property int index
                    required property var modelData

                    readonly property string title: modelData.title

                    width: ListView.view.width
                    height: Theme.navItemHeight
                    radius: 6
                    color: {
                        if (navListView.currentIndex === itemDelegate.index) {
                            return Theme.itemSelected
                        }
                        if (mouseArea.containsMouse) {
                            return Theme.itemHover
                        }
                        return "transparent"
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            navListView.currentIndex = itemDelegate.index
                            navListView.forceActiveFocus()
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingSmall

                        Label {
                            text: itemDelegate.title
                            font.pixelSize: Theme.fontSizeNormal
                            font.bold: navListView.currentIndex === itemDelegate.index
                            color: navListView.currentIndex === itemDelegate.index ? Theme.accent : Theme.text
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // Right: Content sections
            StackLayout {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: navListView.currentIndex

                ScrollView {
                    id: libraryScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical: Controls.AppScrollBar {}

                    ColumnLayout {
                        width: Math.min(libraryScroll.availableWidth, 720)
                        spacing: Theme.spacingLarge

                        SettingsLibrarySection {
                            Layout.fillWidth: true
                        }

                        Item {
                            Layout.preferredHeight: Theme.spacingExtraLarge
                        }
                    }
                }

                ScrollView {
                    id: playbackScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical: Controls.AppScrollBar {}

                    ColumnLayout {
                        width: Math.min(playbackScroll.availableWidth, 720)
                        spacing: Theme.spacingLarge

                        SettingsPlaybackSection {
                            Layout.fillWidth: true
                        }

                        Item {
                            Layout.preferredHeight: Theme.spacingExtraLarge
                        }
                    }
                }

                ScrollView {
                    id: appearanceScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical: Controls.AppScrollBar {}

                    ColumnLayout {
                        width: Math.min(appearanceScroll.availableWidth, 720)
                        spacing: Theme.spacingLarge

                        SettingsAppearanceSection {
                            Layout.fillWidth: true
                        }

                        Item {
                            Layout.preferredHeight: Theme.spacingExtraLarge
                        }
                    }
                }

                ScrollView {
                    id: aiScroll
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical: Controls.AppScrollBar {}

                    ColumnLayout {
                        width: Math.min(aiScroll.availableWidth, 720)
                        spacing: Theme.spacingLarge

                        SettingsAiSection {
                            Layout.fillWidth: true
                        }

                        Item {
                            Layout.preferredHeight: Theme.spacingExtraLarge
                        }
                    }
                }
            }
        }
    }
}
