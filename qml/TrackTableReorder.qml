// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import Linernotes

Item {
    id: root

    property var table
    property var listView
    property int dropTargetRow: -1

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["application/x-linernotes-track-ids"]
        enabled: root.table && root.table.reorderable

        onPositionChanged: (drag) => {
            if (!root.listView || !root.table) {
                return
            }

            const listPos = mapToItem(root.listView, drag.x, drag.y)
            if (listPos.y < 40) {
                root.listView.contentY = Math.max(0, root.listView.contentY - 8)
            } else if (listPos.y > root.listView.height - 40) {
                root.listView.contentY = Math.min(
                    Math.max(0, root.listView.contentHeight - root.listView.height),
                    root.listView.contentY + 8
                )
            }

            const contentPos = mapToItem(root.listView.contentItem, drag.x, drag.y)
            const headerH = root.listView.headerItem ? root.listView.headerItem.height : 0
            const relY = contentPos.y - headerH
            const count = (root.table && root.table.model) ? root.table.model.count : 0

            if (relY < 0) {
                root.dropTargetRow = 0
            } else {
                const row = Math.floor((relY + Theme.tableRowHeight / 2) / Theme.tableRowHeight)
                root.dropTargetRow = Math.max(0, Math.min(count, row))
            }
        }

        onExited: {
            root.dropTargetRow = -1
        }

        onDropped: (drop) => {
            const raw = drop.getDataAsString("application/x-linernotes-track-ids")
            const ids = raw ? raw.split(",").map(Number).filter(id => !isNaN(id) && id > 0) : []
            const beforeRow = root.dropTargetRow
            root.dropTargetRow = -1

            if (ids.length > 0 && root.table && root.table.playlistId > 0 && beforeRow >= 0 && AppContext.playlists) {
                const ok = AppContext.playlists.moveTracks(root.table.playlistId, ids, beforeRow)
                if (!ok) {
                    console.warn("moveTracks failed for playlist:", root.table.playlistId, "tracks:", ids, "beforePosition:", beforeRow)
                }
            }
            drop.acceptProposedAction()
        }
    }

    Rectangle {
        parent: root.listView ? root.listView.contentItem : null
        z: 10
        height: 2
        color: Theme.accent
        width: root.table ? root.table.totalTableWidth : 0
        visible: root.table && root.table.reorderable && dropArea.containsDrag && root.dropTargetRow >= 0
        y: {
            if (root.dropTargetRow < 0) {
                return 0
            }
            const headerH = (root.listView && root.listView.headerItem) ? root.listView.headerItem.height : 0
            return headerH + root.dropTargetRow * Theme.tableRowHeight - 1
        }
    }
}
