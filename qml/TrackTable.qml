// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtCore
import Linernotes
import "TrackColumns.js" as TrackColumns

FocusScope {
    id: root

    readonly property alias count: trackModel.count
    readonly property alias model: trackModel
    readonly property alias selection: rowSelection
    property alias currentIndex: listView.currentIndex

    property bool persistSort: true
    property bool reorderable: false
    property string emptyText: qsTr("No tracks in library")
    property var playlistId: 0
    property int playSource: PlaySource.Library

    property var visibleColumnKeys: TrackColumns.defaultVisibleKeys()
    property var columnWidths: TrackColumns.defaultColumnWidths()

    readonly property var visibleColumns: visibleColumnKeys.map(k => TrackColumns.getColumnByKey(k)).filter(Boolean)

    readonly property real totalTableWidth: getTotalTableWidth()

    function getColumnWidth(key) {
        if (key === "title") {
            var otherSum = 0;
            for (var i = 0; i < visibleColumns.length; i++) {
                if (visibleColumns[i].key !== "title") {
                    var k = visibleColumns[i].key;
                    var w = columnWidths[k] !== undefined ? columnWidths[k] : visibleColumns[i].defaultWidth;
                    otherSum += w;
                }
            }
            return Math.max(200, root.width - otherSum);
        }
        if (columnWidths[key] !== undefined) {
            return columnWidths[key];
        }
        var col = TrackColumns.getColumnByKey(key);
        return col ? col.defaultWidth : 100;
    }

    function getTotalTableWidth() {
        var total = 0;
        for (var i = 0; i < visibleColumns.length; i++) {
            total += getColumnWidth(visibleColumns[i].key);
        }
        return Math.max(root.width, total);
    }

    function setColumnWidth(key, width) {
        var newWidths = Object.assign({}, columnWidths);
        newWidths[key] = Math.max(40, width);
        columnWidths = newWidths;
        tableSettings.columnWidths = JSON.stringify(newWidths);
    }

    function isColumnVisible(key) {
        return visibleColumnKeys.indexOf(key) !== -1;
    }

    function toggleColumnVisibility(key) {
        if (key === "title") return;
        var idx = visibleColumnKeys.indexOf(key);
        var newKeys = visibleColumnKeys.slice();
        if (idx !== -1) {
            newKeys.splice(idx, 1);
        } else {
            var result = [];
            for (var i = 0; i < TrackColumns.ALL_COLUMNS.length; i++) {
                var k = TrackColumns.ALL_COLUMNS[i].key;
                if (k === key || newKeys.indexOf(k) !== -1) {
                    result.push(k);
                }
            }
            newKeys = result;
        }
        visibleColumnKeys = newKeys;
        tableSettings.visibleColumns = newKeys.join(",");
    }

    function restoreDefaultColumns() {
        visibleColumnKeys = TrackColumns.defaultVisibleKeys();
        columnWidths = TrackColumns.defaultColumnWidths();
        tableSettings.visibleColumns = visibleColumnKeys.join(",");
        tableSettings.columnWidths = JSON.stringify(columnWidths);
    }

    function columnTitle(key) {
        switch (key) {
        case "favorite": return ""
        case "trackNumber": return "#"
        //: Track table column header
        case "title": return qsTr("Title")
        //: Track table column header
        case "artist": return qsTr("Artist")
        //: Track table column header
        case "album": return qsTr("Album")
        //: Track table column header
        case "albumArtist": return qsTr("Album Artist")
        //: Track table column header
        case "genre": return qsTr("Genre")
        //: Track table column header
        case "year": return qsTr("Year")
        //: Track table column header
        case "duration": return qsTr("Duration")
        //: Track table column header
        case "rating": return qsTr("Rating")
        //: Track table column header
        case "format": return qsTr("Format")
        //: Track table column header
        case "bitrate": return qsTr("Bitrate")
        //: Track table column header
        case "addedAt": return qsTr("Date Added")
        //: Track table column header
        case "playCount": return qsTr("Plays")
        //: Track table column header
        case "lastPlayed": return qsTr("Last Played")
        default: return ""
        }
    }

    function handleSort(key, sortKeyEnum) {
        if (sortKeyEnum === -1) return;
        if (trackModel.sortKey === sortKeyEnum) {
            trackModel.sortOrder = (trackModel.sortOrder === Qt.AscendingOrder)
                ? Qt.DescendingOrder : Qt.AscendingOrder;
        } else {
            trackModel.sortKey = sortKeyEnum;
            trackModel.sortOrder = Qt.AscendingOrder;
        }
        if (root.persistSort) {
            tableSettings.sortKey = trackModel.sortKey;
            tableSettings.sortOrder = trackModel.sortOrder;
        }
    }

    TrackListModel {
        id: trackModel
        context: AppContext
        playlistId: root.playlistId
    }

    RowSelection {
        id: rowSelection
    }

    TrackContextMenu {
        id: trackContextMenu
        playlistId: root.playlistId
        playSource: root.playSource
    }

    Settings {
        id: tableSettings
        location: AppContext.uiStateUrl
        category: "trackTable"
        property string visibleColumns: "favorite,title,artist,album,year,duration,rating"
        property string columnWidths: "{}"
        property int sortKey: 0
        property int sortOrder: Qt.AscendingOrder
    }

    Component.onCompleted: {
        if (tableSettings.visibleColumns && tableSettings.visibleColumns.length > 0) {
            var cols = tableSettings.visibleColumns.split(",");
            if (cols.indexOf("title") === -1) {
                cols.unshift("title");
            }
            visibleColumnKeys = cols;
        }
        if (tableSettings.columnWidths && tableSettings.columnWidths.length > 0) {
            try {
                var parsed = JSON.parse(tableSettings.columnWidths);
                if (parsed && typeof parsed === "object") {
                    columnWidths = Object.assign({}, TrackColumns.defaultColumnWidths(), parsed);
                }
            } catch (e) {
                // ignore invalid json
            }
        }
        if (root.persistSort) {
            if (tableSettings.sortKey !== undefined && tableSettings.sortKey >= 0) {
                trackModel.sortKey = tableSettings.sortKey;
            }
            if (tableSettings.sortOrder !== undefined) {
                trackModel.sortOrder = tableSettings.sortOrder;
            }
        }
        listView.forceActiveFocus();
    }

    Connections {
        target: trackModel
        function onModelReset() {
            rowSelection.clear();
        }
    }

    Label {
        anchors.centerIn: parent
        visible: trackModel.count === 0 && text.length > 0
        text: root.emptyText
        font.pixelSize: Theme.fontSizeLarge
        color: Theme.textSecondary
    }

    ListView {
        id: listView
        anchors.fill: parent
        clip: true
        focus: true
        reuseItems: true
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        model: trackModel
        contentWidth: root.totalTableWidth
        headerPositioning: ListView.OverlayHeader

        header: TrackTableHeader {
            table: root
            visibleColumns: root.visibleColumns
            contentOffsetX: listView.contentX
            totalWidth: root.totalTableWidth
        }

        delegate: TrackTableRow {
            table: root
            selection: rowSelection
            contextMenu: trackContextMenu
            visibleColumns: root.visibleColumns
        }

        ScrollBar.horizontal: ScrollBar {
            id: hScrollBar
            policy: root.totalTableWidth > listView.width ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
        }

        ScrollBar.vertical: ScrollBar {
            id: vScrollBar
            policy: ScrollBar.AsNeeded
        }

        Keys.onPressed: (event) => {
            const pageSize = Math.max(1, Math.floor(listView.height / Theme.tableRowHeight));
            if (event.key === Qt.Key_Up) {
                const nextRow = Math.max(0, (listView.currentIndex >= 0 ? listView.currentIndex : 0) - 1);
                rowSelection.select(nextRow, event.modifiers);
                listView.currentIndex = nextRow;
                listView.positionViewAtIndex(nextRow, ListView.Contain);
                event.accepted = true;
            } else if (event.key === Qt.Key_Down) {
                const nextRow = Math.min(trackModel.count - 1, (listView.currentIndex >= 0 ? listView.currentIndex : -1) + 1);
                rowSelection.select(nextRow, event.modifiers);
                listView.currentIndex = nextRow;
                listView.positionViewAtIndex(nextRow, ListView.Contain);
                event.accepted = true;
            } else if (event.key === Qt.Key_PageUp) {
                const nextRow = Math.max(0, (listView.currentIndex >= 0 ? listView.currentIndex : 0) - pageSize);
                rowSelection.select(nextRow, event.modifiers);
                listView.currentIndex = nextRow;
                listView.positionViewAtIndex(nextRow, ListView.Contain);
                event.accepted = true;
            } else if (event.key === Qt.Key_PageDown) {
                const nextRow = Math.min(trackModel.count - 1, (listView.currentIndex >= 0 ? listView.currentIndex : 0) + pageSize);
                rowSelection.select(nextRow, event.modifiers);
                listView.currentIndex = nextRow;
                listView.positionViewAtIndex(nextRow, ListView.Contain);
                event.accepted = true;
            } else if (event.key === Qt.Key_Home) {
                if (trackModel.count > 0) {
                    rowSelection.select(0, event.modifiers);
                    listView.currentIndex = 0;
                    listView.positionViewAtIndex(0, ListView.Contain);
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_End) {
                if (trackModel.count > 0) {
                    const last = trackModel.count - 1;
                    rowSelection.select(last, event.modifiers);
                    listView.currentIndex = last;
                    listView.positionViewAtIndex(last, ListView.Contain);
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                rowSelection.selectAll(trackModel.count);
                event.accepted = true;
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (listView.currentIndex >= 0 && listView.currentIndex < trackModel.count && AppContext.actions) {
                    AppContext.actions.playTracks(trackModel.allTrackIds(), listView.currentIndex, root.playSource);
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                if (listView.currentIndex >= 0 && listView.currentIndex < trackModel.count) {
                    const selectedIds = trackModel.trackIds(rowSelection.selectedRows());
                    if (selectedIds.length > 0) {
                        trackContextMenu.popupAt(selectedIds, listView.currentItem || listView);
                    }
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Escape) {
                rowSelection.clear();
                event.accepted = true;
            }
        }
    }

    TrackTableReorder {
        anchors.fill: parent
        table: root
        listView: listView
    }
}
