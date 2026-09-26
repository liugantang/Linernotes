// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Linernotes

Rectangle {
    id: root

    property alias collapsed: panelSettings.collapsed

    Settings {
        id: panelSettings
        location: AppContext.uiStateUrl
        category: "SidePanel"
        property bool collapsed: false
    }

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton {
                text: qsTr("Queue")
            }
            TabButton {
                text: qsTr("Lyrics")
            }
            TabButton {
                text: qsTr("Guide")
            }
            TabButton {
                text: qsTr("DJ")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        StackLayout {
            id: panelStack
            currentIndex: tabBar.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Queue tab
            Item {
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall
                    Label {
                        text: qsTr("Queue")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.text
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: qsTr("Playback queue content will appear here.")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // Lyrics tab
            Item {
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall
                    Label {
                        text: qsTr("Lyrics")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.text
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: qsTr("Synchronized lyrics will appear here.")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // Guide tab
            Item {
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall
                    Label {
                        text: qsTr("Guide")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.text
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: qsTr("AI music guide and liner notes.")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // DJ tab
            Item {
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall
                    Label {
                        text: qsTr("DJ Subtitles")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.text
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: qsTr("Live AI DJ commentary and subtitles.")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }
}
