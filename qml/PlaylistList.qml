// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    property int selectedPlaylistId: 0
    property string targetPlaylistName: ""
    property int targetPlaylistId: 0
    property bool targetIsSmart: false
    property bool isRenaming: false
    property string feedbackText: ""

    readonly property var playlistModel: AppContext.playlists ? AppContext.playlists.model : null

    function openSmartRuleDialog(id, name) {
        smartRuleDialog.openEdit(id, name)
    }

    function openNewSmartRuleDialog() {
        smartRuleDialog.openNew()
    }

    function showFeedback(text) {
        feedbackText = text
        feedbackTimer.restart()
    }

    Timer {
        id: feedbackTimer
        interval: 2000
        repeat: false
        onTriggered: {
            root.feedbackText = ""
        }
    }

    SmartRuleDialog {
        id: smartRuleDialog
        onPlaylistCreated: (newId) => {
            root.selectedPlaylistId = newId
        }
    }

    PlaylistNameDialog {
        id: nameDialog
        titleText: root.isRenaming ? qsTr("Rename Playlist") : qsTr("New Playlist")
        acceptButtonText: root.isRenaming ? qsTr("Rename") : qsTr("Create")
        onAccepted: (name) => {
            if (!AppContext.playlists) return
            if (root.isRenaming) {
                AppContext.playlists.rename(root.targetPlaylistId, name)
            } else {
                const newId = AppContext.playlists.createManual(name)
                if (newId > 0) {
                    root.selectedPlaylistId = newId
                }
            }
        }
    }

    Popup {
        id: deleteDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        implicitWidth: 360
        implicitHeight: deleteLayout.implicitHeight + topPadding + bottomPadding
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

        contentItem: ColumnLayout {
            id: deleteLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Delete Playlist")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Are you sure you want to delete \"%1\"?").arg(root.targetPlaylistName)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Item {
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: deleteDialog.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Delete")
                    onClicked: {
                        deleteDialog.close()
                        if (AppContext.playlists) {
                            AppContext.playlists.remove(root.targetPlaylistId)
                            if (root.selectedPlaylistId === root.targetPlaylistId) {
                                root.selectedPlaylistId = 0
                            }
                        }
                    }
                }
            }
        }
    }

    Controls.AppMenu {
        id: playlistItemMenu

        Controls.AppMenuItem {
            text: qsTr("Edit Rules...")
            visible: root.targetIsSmart
            height: visible ? implicitHeight : 0
            onTriggered: {
                smartRuleDialog.openEdit(root.targetPlaylistId, root.targetPlaylistName)
            }
        }

        Controls.AppMenuItem {
            text: qsTr("Rename...")
            onTriggered: {
                root.isRenaming = true
                nameDialog.openWithText(root.targetPlaylistName)
            }
        }

        Controls.AppMenuItem {
            text: qsTr("Delete...")
            onTriggered: {
                deleteDialog.open()
            }
        }
    }

    Controls.AppMenu {
        id: newPlaylistMenu

        Controls.AppMenuItem {
            text: qsTr("New Playlist...")
            icon.source: "icons/list-music.svg"
            onTriggered: {
                root.isRenaming = false
                nameDialog.openWithText("")
            }
        }

        Controls.AppMenuItem {
            text: qsTr("New Smart Playlist...")
            icon.source: "icons/sparkles.svg"
            onTriggered: {
                smartRuleDialog.openNew()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Top action bar
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Playlists")
                font.pixelSize: Theme.fontSizeNormal
                font.bold: true
                color: Theme.text
            }

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("New")
                icon.source: "icons/list-plus.svg"
                onClicked: newPlaylistMenu.popup(this, 0, height)
            }
        }

        // Notification / Feedback banner
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.feedbackText.length > 0 ? 28 : 0
            visible: root.feedbackText.length > 0
            color: Theme.surfaceVariant
            clip: true

            Label {
                anchors.centerIn: parent
                text: root.feedbackText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.accent
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: playlistListView
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.playlistModel

                delegate: Rectangle {
                    id: playlistDelegate
                    required property var model
                    required property int index

                    readonly property bool isSelected: root.selectedPlaylistId === model.playlistId

                    width: playlistListView.width
                    height: Theme.tableRowHeight
                    radius: Theme.radiusSmall
                    color: isSelected ? Theme.itemSelected
                        : (playlistDropArea.containsDrag ? Theme.accentHover
                        : (itemMouseArea.containsMouse ? Theme.hoverOverlay : "transparent"))

                    DropArea {
                        id: playlistDropArea
                        anchors.fill: parent
                        keys: ["application/x-linernotes-track-ids"]
                        enabled: !playlistDelegate.model.isSmart

                        onDropped: (drop) => {
                            const raw = drop.getDataAsString("application/x-linernotes-track-ids")
                            const ids = raw ? raw.split(",").map(Number).filter(id => !isNaN(id) && id > 0) : []
                            if (ids.length > 0 && AppContext.playlists) {
                                const added = AppContext.playlists.addTracks(playlistDelegate.model.playlistId, ids)
                                if (added > 0) {
                                    root.showFeedback(qsTr("Added %n track(s)", "", added))
                                }
                            }
                            drop.acceptProposedAction()
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        spacing: Theme.spacingSmall

                        Image {
                            source: model.isSmart ? "icons/sparkles.svg" : "icons/list-music.svg"
                            Layout.preferredWidth: Theme.iconSize
                            Layout.preferredHeight: Theme.iconSize
                            sourceSize: Qt.size(Theme.iconSize, Theme.iconSize)
                        }

                        Label {
                            text: model.name
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    MouseArea {
                        id: itemMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onClicked: (mouse) => {
                            if (mouse.button === Qt.LeftButton) {
                                root.selectedPlaylistId = playlistDelegate.model.playlistId
                            } else if (mouse.button === Qt.RightButton) {
                                root.selectedPlaylistId = playlistDelegate.model.playlistId
                                root.targetPlaylistId = playlistDelegate.model.playlistId
                                root.targetPlaylistName = playlistDelegate.model.name
                                root.targetIsSmart = playlistDelegate.model.isSmart
                                playlistItemMenu.popup(playlistDelegate, mouse.x, mouse.y)
                            }
                        }
                    }
                }

                ScrollBar.vertical: Controls.AppScrollBar {}
            }

            Label {
                anchors.centerIn: parent
                visible: root.playlistModel === null || root.playlistModel.count === 0
                text: qsTr("No playlists")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }
        }
    }
}
