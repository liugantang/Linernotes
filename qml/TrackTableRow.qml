// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import Linernotes
import "TrackColumns.js" as TrackColumns

Rectangle {
    id: rootRow

    required property var table
    required property var selection
    required property var contextMenu
    required property var visibleColumns
    required property int index

    required property var trackId
    required property var trackNumber
    required property string title
    required property string artist
    required property string album
    required property string albumArtist
    required property string genre
    required property var year
    required property string durationText
    required property string codec
    required property int sampleRate
    required property int bitDepth
    required property int bitrate
    required property var addedAt

    readonly property bool isCurrent: rootRow.table.currentIndex === rootRow.index && rootRow.table.activeFocus
    readonly property bool isSelectedRow: rootRow.selection.revision >= 0 && rootRow.selection.isSelected(rootRow.index)

    width: rootRow.table.totalTableWidth
    height: Theme.tableRowHeight
    color: {
        if (isSelectedRow) {
            return Theme.itemSelected
        }
        if (rowMouseArea.containsMouse) {
            return Theme.hoverOverlay
        }
        return "transparent"
    }

    function cellText(key) {
        switch (key) {
        case "trackNumber":
            return (trackNumber !== undefined && trackNumber !== null && trackNumber > 0)
                ? String(trackNumber) : ""
        case "title":
            return title || ""
        case "artist":
            return artist || ""
        case "album":
            return album || ""
        case "albumArtist":
            return albumArtist || ""
        case "genre":
            return genre || ""
        case "year":
            return (year !== undefined && year !== null && year > 0)
                ? String(year) : ""
        case "duration":
            return durationText || ""
        case "format":
            return TrackColumns.formatCodec(codec, sampleRate, bitDepth)
        case "bitrate":
            return (bitrate !== undefined && bitrate !== null && bitrate > 0)
                ? (bitrate + " kbps") : ""
        case "addedAt":
            return TrackColumns.formatAddedAt(addedAt)
        default:
            return ""
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: rootRow.visibleColumns

            delegate: Item {
                id: cellItem
                required property var modelData
                required property int index

                width: rootRow.table.getColumnWidth(modelData.key)
                height: rootRow.height
                clip: true

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: modelData.alignRight ? Text.AlignRight : Text.AlignLeft
                    text: rootRow.cellText(modelData.key)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: rootRow.isSelectedRow ? Theme.text : Theme.text
                    font.pixelSize: Theme.fontSizeNormal
                }
            }
        }
    }

    MouseArea {
        id: rowMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (mouse) => {
            rootRow.table.forceActiveFocus()
            if (mouse.button === Qt.LeftButton) {
                rootRow.selection.select(rootRow.index, mouse.modifiers)
                rootRow.table.currentIndex = rootRow.index
            } else if (mouse.button === Qt.RightButton) {
                if (!rootRow.selection.isSelected(rootRow.index)) {
                    rootRow.selection.select(rootRow.index, Qt.NoModifier)
                    rootRow.table.currentIndex = rootRow.index
                }
                rootRow.contextMenu.popupFor(rootRow.table.model.trackIds(rootRow.selection.selectedRows()))
            }
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                if (AppContext.actions) {
                    AppContext.actions.playTracks(rootRow.table.model.allTrackIds(), rootRow.index)
                }
            }
        }
    }
}
