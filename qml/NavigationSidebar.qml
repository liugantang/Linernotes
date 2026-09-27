// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes

Rectangle {
    id: root

    enum Page {
        Tracks,
        Albums,
        Artists,
        Playlists,
        Ai,
        Settings
    }

    property int currentPage: NavigationSidebar.Tracks

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    readonly property var navModel: [
        { pageId: NavigationSidebar.Tracks, title: qsTr("Tracks"), iconSource: "library", subtitle: "" },
        { pageId: NavigationSidebar.Albums, title: qsTr("Albums"), iconSource: "disc-3", subtitle: "" },
        { pageId: NavigationSidebar.Artists, title: qsTr("Artists"), iconSource: "mic-vocal", subtitle: "" },
        { pageId: NavigationSidebar.Playlists, title: qsTr("Playlists"), iconSource: "list-music", subtitle: "" },
        { pageId: NavigationSidebar.Ai, title: qsTr("AI"), iconSource: "sparkles", subtitle: qsTr("Coming soon") }
    ]

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
            model: root.navModel
            clip: true
            focus: true
            keyNavigationEnabled: true
            keyNavigationWraps: false
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: itemDelegate
                required property int index
                required property var modelData

                readonly property int pageId: modelData.pageId
                readonly property string title: modelData.title
                readonly property string subtitle: modelData.subtitle
                readonly property string iconSource: modelData.iconSource

                width: ListView.view.width
                height: Theme.navItemHeight
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

                DropArea {
                    anchors.fill: parent
                    keys: ["application/x-linernotes-track-ids"]
                    enabled: itemDelegate.pageId === NavigationSidebar.Playlists

                    onEntered: {
                        hoverTimer.restart()
                    }
                    onExited: {
                        hoverTimer.stop()
                    }

                    Timer {
                        id: hoverTimer
                        interval: 600
                        repeat: false
                        onTriggered: {
                            listView.currentIndex = itemDelegate.index
                            root.currentPage = itemDelegate.pageId
                        }
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingMedium
                    anchors.rightMargin: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    // IconImage 与控件的 icon.color 同一套着色实现（单色 SVG 按 color 重新着色）
                    IconImage {
                        source: "icons/" + itemDelegate.iconSource + ".svg"
                        Layout.preferredWidth: Theme.iconSize
                        Layout.preferredHeight: Theme.iconSize
                        sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
                        color: root.currentPage === itemDelegate.pageId ? Theme.accent : Theme.text
                    }

                    Label {
                        text: itemDelegate.title
                        font.pixelSize: Theme.fontSizeNormal
                        font.bold: root.currentPage === itemDelegate.pageId
                        color: root.currentPage === itemDelegate.pageId ? Theme.accent : Theme.text
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        visible: itemDelegate.subtitle.length > 0
                        color: Theme.surfaceVariant
                        radius: Theme.radiusSmall
                        Layout.preferredHeight: subtitleLabel.implicitHeight + Theme.spacingTiny
                        Layout.preferredWidth: subtitleLabel.implicitWidth + Theme.spacingSmall
                        
                        Label {
                            id: subtitleLabel
                            anchors.centerIn: parent
                            text: itemDelegate.subtitle
                            font.pixelSize: Theme.fontSizeSmall - 2
                            color: Theme.textSecondary
                        }
                    }
                }
            }

            Keys.onReturnPressed: {
                if (currentIndex >= 0 && currentIndex < count) {
                    root.currentPage = root.navModel[currentIndex].pageId
                }
            }
            Keys.onEnterPressed: {
                if (currentIndex >= 0 && currentIndex < count) {
                    root.currentPage = root.navModel[currentIndex].pageId
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

        // Settings item at bottom
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Rectangle {
            id: settingsItem
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.navItemHeight
            radius: 6
            color: {
                if (root.currentPage === NavigationSidebar.Settings) {
                    return Theme.itemSelected
                }
                if (settingsMouseArea.containsMouse) {
                    return Theme.itemHover
                }
                return "transparent"
            }

            MouseArea {
                id: settingsMouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    listView.currentIndex = -1
                    root.currentPage = NavigationSidebar.Settings
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall

                IconImage {
                    source: "icons/settings.svg"
                    Layout.preferredWidth: Theme.iconSize
                    Layout.preferredHeight: Theme.iconSize
                    sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
                    color: root.currentPage === NavigationSidebar.Settings ? Theme.accent : Theme.text
                }

                Label {
                    text: qsTr("Settings")
                    font.pixelSize: Theme.fontSizeNormal
                    font.bold: root.currentPage === NavigationSidebar.Settings
                    color: root.currentPage === NavigationSidebar.Settings ? Theme.accent : Theme.text
                    Layout.fillWidth: true
                }
            }
        }
    }
}
