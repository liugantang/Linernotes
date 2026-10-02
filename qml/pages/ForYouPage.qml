// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes
import "../controls" as Controls

Item {
    id: root

    readonly property bool isForYou: AppContext.recommend && AppContext.recommend.kind === RecommendController.ForYou

    function loadRecommendations() {
        if (AppContext.recommend) {
            AppContext.recommend.load()
        }
    }

    StackLayout.onIsCurrentItemChanged: {
        if (StackLayout.isCurrentItem) {
            root.loadRecommendations()
        }
    }

    onVisibleChanged: {
        if (visible) {
            root.loadRecommendations()
        }
    }

    Component.onCompleted: {
        if (visible || (StackLayout.isCurrentItem !== undefined && StackLayout.isCurrentItem)) {
            root.loadRecommendations()
        }
    }

    PlaylistNameDialog {
        id: savePlaylistDialog
        titleText: qsTr("Save as Playlist")
        acceptButtonText: qsTr("Save")
        onAccepted: (name) => {
            if (AppContext.recommend) {
                AppContext.recommend.saveAsPlaylist(name)
            }
        }
    }

    TrackContextMenu {
        id: contextMenu
        playSource: PlaySource.Playlist
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Controls.AppTabBar {
                id: kindTabBar
                currentIndex: root.isForYou ? 1 : 0
                onCurrentIndexChanged: {
                    if (AppContext.recommend) {
                        AppContext.recommend.kind = (currentIndex === 1)
                            ? RecommendController.ForYou
                            : RecommendController.Daily
                    }
                }

                Controls.AppTabButton {
                    text: qsTr("Daily Mix")
                }
                Controls.AppTabButton {
                    text: qsTr("For You")
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                visible: root.isForYou
                text: qsTr("Refresh")
                icon.source: "../icons/rotate-ccw.svg"
                onClicked: {
                    if (AppContext.recommend) {
                        AppContext.recommend.refresh()
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Play All")
                primary: true
                enabled: !!(AppContext.recommend && AppContext.recommend.rows.length > 0)
                onClicked: {
                    if (AppContext.recommend) {
                        AppContext.recommend.playAll()
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Add to Queue")
                enabled: !!(AppContext.recommend && AppContext.recommend.rows.length > 0)
                onClicked: {
                    if (AppContext.recommend) {
                        AppContext.recommend.enqueueAll()
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Save as Playlist")
                enabled: !!(AppContext.recommend && AppContext.recommend.rows.length > 0)
                onClicked: {
                    const now = new Date()
                    const pad = (n) => (n < 10 ? "0" + n : "" + n)
                    const dateStr = now.getFullYear() + "-" + pad(now.getMonth() + 1) + "-" + pad(now.getDate())
                    const defaultName = root.isForYou
                        ? qsTr("For You %1").arg(dateStr)
                        : qsTr("Daily Mix %1").arg(dateStr)
                    savePlaylistDialog.openWithText(defaultName)
                }
            }
        }

        // Subtitle description
        Label {
            text: root.isForYou
                ? qsTr("Tracks you haven't heard in a while, or ever, picked to match your recent taste.")
                : qsTr("A fixed mix for today: favorites you play often plus new discoveries.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Content Area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Empty state
            Item {
                anchors.fill: parent
                visible: !AppContext.recommend || AppContext.recommend.rows.length === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingMedium
                    width: Math.min(parent.width - Theme.spacingLarge * 2, 400)

                    IconImage {
                        source: "../icons/compass.svg"
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        sourceSize: Qt.size(48, 48)
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        text: qsTr("Nothing to recommend yet. Play some music first.")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                }
            }

            // Recommendation Results List
            ListView {
                id: resultListView
                anchors.fill: parent
                visible: !!(AppContext.recommend && AppContext.recommend.rows.length > 0)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                focus: true
                keyNavigationEnabled: true
                model: AppContext.recommend ? AppContext.recommend.rows : []

                ScrollBar.vertical: Controls.AppScrollBar {}

                Keys.onReturnPressed: (event) => {
                    if (currentIndex >= 0 && AppContext.recommend) {
                        AppContext.recommend.playRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: (event) => {
                    if (currentIndex >= 0 && AppContext.recommend) {
                        AppContext.recommend.playRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                        if (currentIndex >= 0 && AppContext.recommend && currentIndex < AppContext.recommend.rows.length) {
                            const row = AppContext.recommend.rows[currentIndex]
                            if (row && row.trackId) {
                                contextMenu.popupAt([row.trackId], currentItem || resultListView)
                            }
                        }
                        event.accepted = true
                    }
                }

                delegate: Rectangle {
                    id: rowDelegate
                    required property int index
                    required property var modelData

                    width: resultListView.width - (resultListView.ScrollBar.vertical.visible ? resultListView.ScrollBar.vertical.width : 0)
                    height: Theme.trackRowHeight
                    radius: Theme.radiusSmall
                    color: {
                        if (resultListView.currentIndex === index && resultListView.activeFocus) {
                            return Theme.itemSelected
                        }
                        if (rowMouseArea.containsMouse) {
                            return Theme.itemHover
                        }
                        return "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: Theme.radiusSmall
                            color: Theme.surfaceVariant
                            clip: true

                            Image {
                                anchors.fill: parent
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                source: (rowDelegate.modelData && rowDelegate.modelData.coverHash)
                                    ? ("image://cover/" + encodeURIComponent(rowDelegate.modelData.coverHash)) : ""
                                sourceSize: Qt.size(32, 32)
                                visible: status === Image.Ready
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: rowDelegate.modelData ? (rowDelegate.modelData.title || qsTr("Unknown Title")) : ""
                                font.pixelSize: Theme.fontSizeNormal
                                font.bold: true
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: {
                                    if (!rowDelegate.modelData) return ""
                                    const artist = rowDelegate.modelData.artist || qsTr("Unknown Artist")
                                    const album = rowDelegate.modelData.album || ""
                                    return album.length > 0 ? (artist + " · " + album) : artist
                                }
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: rowDelegate.modelData ? (rowDelegate.modelData.durationText || "0:00") : "0:00"
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 50
                        }
                    }

                    MouseArea {
                        id: rowMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            resultListView.currentIndex = rowDelegate.index
                            resultListView.forceActiveFocus()
                            if (mouse.button === Qt.RightButton && rowDelegate.modelData && rowDelegate.modelData.trackId) {
                                contextMenu.popupFor([rowDelegate.modelData.trackId])
                            }
                        }
                        onDoubleClicked: (mouse) => {
                            if (mouse.button === Qt.LeftButton && AppContext.recommend) {
                                AppContext.recommend.playRow(rowDelegate.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
