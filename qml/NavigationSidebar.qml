// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Rectangle {
    id: root

    property string currentPage: "tracks"

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    ListModel {
        id: navModel
        ListElement { pageId: "tracks"; title: qsTr("Tracks"); subtitle: "" }
        ListElement { pageId: "albums"; title: qsTr("Albums"); subtitle: "" }
        ListElement { pageId: "artists"; title: qsTr("Artists"); subtitle: "" }
        ListElement { pageId: "playlists"; title: qsTr("Playlists"); subtitle: "" }
        ListElement { pageId: "ai"; title: qsTr("AI"); subtitle: qsTr("Coming soon") }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        // App title header
        Label {
            text: AppInfo.name
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
            color: Theme.accent
            Layout.fillWidth: true
            Layout.margins: Theme.spacingSmall
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: navModel
            clip: true
            focus: true
            keyNavigationEnabled: true
            keyNavigationWraps: false
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: itemDelegate
                required property int index
                required property string pageId
                required property string title
                required property string subtitle

                width: ListView.view.width
                height: subtitle.length > 0 ? Theme.navItemHeightWithSubtitle : Theme.navItemHeight
                radius: 6
                color: {
                    if (root.currentPage === pageId) {
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
                        listView.currentIndex = itemDelegate.index
                        root.currentPage = itemDelegate.pageId
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    anchors.topMargin: Theme.spacingSmall
                    anchors.bottomMargin: Theme.spacingSmall
                    spacing: 2

                    Label {
                        text: itemDelegate.title
                        font.pixelSize: Theme.fontSizeNormal
                        font.bold: root.currentPage === itemDelegate.pageId
                        color: root.currentPage === itemDelegate.pageId ? Theme.accent : Theme.text
                        Layout.fillWidth: true
                    }

                    Label {
                        visible: itemDelegate.subtitle.length > 0
                        text: itemDelegate.subtitle
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.fillWidth: true
                    }
                }
            }

            Keys.onReturnPressed: {
                if (currentIndex >= 0 && currentIndex < count) {
                    root.currentPage = navModel.get(currentIndex).pageId
                }
            }
            Keys.onEnterPressed: {
                if (currentIndex >= 0 && currentIndex < count) {
                    root.currentPage = navModel.get(currentIndex).pageId
                }
            }
        }

        // Scanning indicator at bottom
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
            visible: AppContext.scanning
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingSmall
            spacing: Theme.spacingSmall
            visible: AppContext.scanning

            BusyIndicator {
                Layout.preferredWidth: Theme.iconSizeSmall
                Layout.preferredHeight: Theme.iconSizeSmall
                running: AppContext.scanning
            }

            Label {
                text: qsTr("Scanning library…")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }
}
