// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

FocusScope {
    id: root

    readonly property var queueModel: AppContext.queueModel
    readonly property int queueCount: (AppContext.player && AppContext.player.queue)
        ? AppContext.player.queue.count : 0
    readonly property alias selection: rowSelection
    property alias currentIndex: listView.currentIndex

    property int dragFromIndex: -1
    property int dropTargetIndex: -1

    property string notificationText: ""
    property bool notificationIsError: false

    RowSelection {
        id: rowSelection
    }

    PlaylistNameDialog {
        id: saveDialog
        titleText: qsTr("Save as Playlist")
        acceptButtonText: qsTr("Save")
        onAccepted: (name) => {
            if (AppContext.playlists) {
                const res = AppContext.playlists.saveQueue(name)
                if (res > 0) {
                    root.showNotification(qsTr("Saved as playlist: %1").arg(name), false)
                } else {
                    root.showNotification(qsTr("Failed to save playlist"), true)
                }
            }
        }
    }

    Timer {
        id: notificationTimer
        interval: 2000
        repeat: false
        onTriggered: {
            root.notificationText = ""
        }
    }

    function showNotification(text, isError) {
        root.notificationText = text
        root.notificationIsError = isError
        notificationTimer.restart()
    }

    function openContextMenu(index, trackId, item) {
        contextMenu.targetRow = index
        contextMenu.targetTrackId = (trackId !== undefined && trackId !== null) ? trackId : -1
        contextMenu.popup(item, 0, item.height)
    }

    function startDrag(fromIndex, mouseY) {
        dragFromIndex = fromIndex
        listView.interactive = false
        updateDrag(mouseY)
    }

    function updateDrag(mouseY) {
        if (dragFromIndex < 0 || root.queueCount === 0) return

        const contentPos = root.mapToItem(listView.contentItem, listView.width / 2, mouseY)
        let target = listView.indexAt(contentPos.x, contentPos.y)
        if (target === -1) {
            if (contentPos.y <= 0) {
                target = 0
            } else {
                target = root.queueCount - 1
            }
        }
        target = Math.max(0, Math.min(root.queueCount - 1, target))
        dropTargetIndex = target

        // Auto-scroll when dragging near list edges
        const listPos = root.mapToItem(listView, 0, mouseY)
        if (listPos.y < 30) {
            listView.contentY = Math.max(0, listView.contentY - 8)
        } else if (listPos.y > listView.height - 30) {
            listView.contentY = Math.min(
                Math.max(0, listView.contentHeight - listView.height),
                listView.contentY + 8
            )
        }
    }

    function endDrag() {
        listView.interactive = true
        if (dragFromIndex >= 0 && dropTargetIndex >= 0 && dragFromIndex !== dropTargetIndex) {
            if (root.queueModel) {
                root.queueModel.move(dragFromIndex, dropTargetIndex)
                rowSelection.select(dropTargetIndex, Qt.NoModifier)
                listView.currentIndex = dropTargetIndex
            }
        }
        dragFromIndex = -1
        dropTargetIndex = -1
    }

    function ensureCurrentVisible() {
        if (AppContext.player && AppContext.player.queue) {
            const current = AppContext.player.queue.currentIndex
            if (current >= 0 && current < root.queueCount) {
                listView.positionViewAtIndex(current, ListView.Contain)
            }
        }
    }

    Component.onCompleted: {
        ensureCurrentVisible()
    }

    onVisibleChanged: {
        if (visible) {
            ensureCurrentVisible()
        }
    }

    Connections {
        target: (AppContext.player && AppContext.player.queue) ? AppContext.player.queue : null
        function onCurrentIndexChanged(index) {
            if (root.visible && index >= 0) {
                listView.positionViewAtIndex(index, ListView.Contain)
            }
        }
    }

    Controls.AppMenu {
        id: contextMenu
        property int targetRow: -1
        property var targetTrackId: -1

        Controls.AppMenuItem {
            text: qsTr("Play")
            onTriggered: {
                if (contextMenu.targetRow >= 0 && root.queueModel) {
                    root.queueModel.playAt(contextMenu.targetRow)
                }
            }
        }

        Controls.AppMenuItem {
            text: qsTr("Remove")
            enabled: {
                const rows = rowSelection.selectedRows()
                const current = (AppContext.player && AppContext.player.queue)
                    ? AppContext.player.queue.currentIndex : -1
                if (rows.length === 1 && rows[0] === current) return false
                return rows.length > 0 || contextMenu.targetRow !== current
            }
            onTriggered: {
                if (root.queueModel) {
                    const rows = rowSelection.selectedRows()
                    if (rows.indexOf(contextMenu.targetRow) !== -1) {
                        root.queueModel.removeItems(rows)
                    } else if (contextMenu.targetRow >= 0) {
                        root.queueModel.removeItems([contextMenu.targetRow])
                    }
                }
            }
        }

        Controls.AppMenuSeparator { }

        Controls.AppMenuItem {
            text: qsTr("Show in File Manager")
            enabled: contextMenu.targetTrackId > 0
            onTriggered: {
                if (contextMenu.targetTrackId > 0 && AppContext.actions) {
                    AppContext.actions.showInFileManager(contextMenu.targetTrackId)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Queue")
                font.pixelSize: Theme.fontSizeNormal
                font.bold: true
                color: Theme.text
            }

            Label {
                text: qsTr("%n track(s)", "", root.queueCount)
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }

            Item {
                Layout.fillWidth: true
            }

            Controls.IconButton {
                icon.source: "icons/list-plus.svg"
                toolTip: qsTr("Save as Playlist")
                enabled: root.queueCount > 0
                onClicked: {
                    const defaultName = qsTr("Queue %1").arg(Qt.formatDateTime(new Date(), "yyyy-MM-dd HH:mm"))
                    saveDialog.openWithText(defaultName)
                }
            }

            Controls.IconButton {
                icon.source: "icons/trash-2.svg"
                toolTip: qsTr("Clear queue (keeps currently playing track)")
                enabled: root.queueCount > 0
                onClicked: {
                    if (root.queueModel) {
                        root.queueModel.clear()
                    }
                }
            }
        }

        // Notification banner
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.notificationText.length > 0 ? 32 : 0
            visible: root.notificationText.length > 0
            color: root.notificationIsError ? Theme.errorBackground : Theme.surfaceVariant
            clip: true

            Label {
                anchors.centerIn: parent
                text: root.notificationText
                font.pixelSize: Theme.fontSizeSmall
                color: root.notificationIsError ? Theme.errorText : Theme.textSecondary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // List / Empty placeholder
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: listView
                anchors.fill: parent
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.queueModel

                delegate: QueueRow {
                    queuePanel: root
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                // Insertion indicator line
                Rectangle {
                    parent: listView.contentItem
                    z: 10
                    height: 2
                    color: Theme.accent
                    width: listView.width
                    visible: root.dragFromIndex >= 0 && root.dropTargetIndex >= 0
                        && root.dragFromIndex !== root.dropTargetIndex
                    y: {
                        if (root.dropTargetIndex < 0) return 0
                        const rowH = Theme.trackRowHeight
                        if (root.dropTargetIndex > root.dragFromIndex) {
                            return (root.dropTargetIndex + 1) * rowH - 1
                        } else {
                            return root.dropTargetIndex * rowH
                        }
                    }
                }

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Up) {
                        if (event.modifiers & Qt.AltModifier) {
                            if (listView.currentIndex > 0 && root.queueModel) {
                                const from = listView.currentIndex
                                const to = from - 1
                                root.queueModel.move(from, to)
                                listView.currentIndex = to
                                rowSelection.select(to, Qt.NoModifier)
                                listView.positionViewAtIndex(to, ListView.Contain)
                                event.accepted = true
                            }
                        } else {
                            const nextRow = Math.max(
                                0,
                                (listView.currentIndex >= 0 ? listView.currentIndex : 0) - 1
                            )
                            rowSelection.select(nextRow, event.modifiers)
                            listView.currentIndex = nextRow
                            listView.positionViewAtIndex(nextRow, ListView.Contain)
                            event.accepted = true
                        }
                    } else if (event.key === Qt.Key_Down) {
                        if (event.modifiers & Qt.AltModifier) {
                            if (listView.currentIndex >= 0
                                && listView.currentIndex < root.queueCount - 1
                                && root.queueModel) {
                                const from = listView.currentIndex
                                const to = from + 1
                                root.queueModel.move(from, to)
                                listView.currentIndex = to
                                rowSelection.select(to, Qt.NoModifier)
                                listView.positionViewAtIndex(to, ListView.Contain)
                                event.accepted = true
                            }
                        } else {
                            const nextRow = Math.min(
                                root.queueCount - 1,
                                (listView.currentIndex >= 0 ? listView.currentIndex : -1) + 1
                            )
                            rowSelection.select(nextRow, event.modifiers)
                            listView.currentIndex = nextRow
                            listView.positionViewAtIndex(nextRow, ListView.Contain)
                            event.accepted = true
                        }
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (listView.currentIndex >= 0
                            && listView.currentIndex < root.queueCount
                            && root.queueModel) {
                            root.queueModel.playAt(listView.currentIndex)
                            event.accepted = true
                        }
                    } else if (event.key === Qt.Key_Delete) {
                        if (root.queueModel) {
                            root.queueModel.removeItems(rowSelection.selectedRows())
                            event.accepted = true
                        }
                    } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                        rowSelection.selectAll(root.queueCount)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        rowSelection.clear()
                        event.accepted = true
                    }
                }
            }

            // Empty state placeholder
            Item {
                anchors.centerIn: parent
                visible: root.queueCount === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Label {
                        text: qsTr("Queue is empty")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        text: qsTr("Double-click tracks in the library to start playing")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }
        }
    }
}
