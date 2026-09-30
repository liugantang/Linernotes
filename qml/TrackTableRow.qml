// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls
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
    required property bool favorite
    required property int rating
    required property var addedAt
    required property int playCount
    required property var lastPlayedAt
    required property int versionType
    required property string titleTranslated
    required property string albumTranslated

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

    function escapeHtml(str) {
        if (!str) return ""
        return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    function albumDisplayText() {
        if (!album) return ""
        if (AppContext.settings && AppContext.settings.showTranslations && albumTranslated && albumTranslated.length > 0) {
            return escapeHtml(album) + " <font color=\"" + Theme.textSecondary + "\">· " + escapeHtml(albumTranslated) + "</font>"
        }
        return album
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
            return TrackColumns.formatDate(addedAt)
        case "playCount":
            return playCount > 0 ? String(playCount) : ""
        case "lastPlayed":
            return TrackColumns.formatDate(lastPlayedAt)
        default:
            return ""
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0
        z: 1

        Repeater {
            model: rootRow.visibleColumns

            delegate: Item {
                id: cellItem
                required property var modelData
                required property int index

                width: rootRow.table.getColumnWidth(modelData.key)
                height: rootRow.height
                clip: true

                Controls.FavoriteButton {
                    visible: modelData.key === "favorite"
                    anchors.centerIn: parent
                    favorite: rootRow.favorite
                    onFavoriteToggled: {
                        if (AppContext.marks && rootRow.trackId) {
                            AppContext.marks.toggleFavorite(Library.FavoriteKind.Track, rootRow.trackId)
                        }
                    }
                }

                Controls.RatingStars {
                    visible: modelData.key === "rating"
                    anchors.centerIn: parent
                    rating: rootRow.rating
                    onRated: (value) => {
                        if (AppContext.marks && rootRow.trackId) {
                            AppContext.marks.setRating([rootRow.trackId], value)
                        }
                    }
                }

                RowLayout {
                    visible: modelData.key === "title"
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    spacing: Theme.spacingTiny

                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                        verticalAlignment: Text.AlignVCenter
                        text: rootRow.cellText("title")
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: rootRow.isSelectedRow ? Theme.text : Theme.text
                        font.pixelSize: Theme.fontSizeNormal
                    }

                    Label {
                        visible: AppContext.settings && AppContext.settings.showTranslations && rootRow.titleTranslated.length > 0
                        text: rootRow.titleTranslated
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        verticalAlignment: Text.AlignVCenter
                        Layout.maximumWidth: (cellItem.width - Theme.spacingSmall * 2) * 0.4
                    }

                    VersionBadge {
                        versionType: rootRow.versionType
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Label {
                    visible: modelData.key !== "title" && modelData.key !== "favorite" && modelData.key !== "rating"
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: modelData.alignRight ? Text.AlignRight : Text.AlignLeft
                    text: modelData.key === "album" ? rootRow.albumDisplayText() : rootRow.cellText(modelData.key)
                    textFormat: (modelData.key === "album" && AppContext.settings && AppContext.settings.showTranslations && rootRow.albumTranslated.length > 0) ? Text.StyledText : Text.PlainText
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

        property point pressPos: Qt.point(0, 0)
        property bool dragStarted: false

        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                pressPos = Qt.point(mouse.x, mouse.y)
                dragStarted = false
                preventStealing = true
            }
        }

        onPositionChanged: (mouse) => {
            if ((mouse.buttons & Qt.LeftButton) && !dragStarted) {
                const dx = mouse.x - pressPos.x
                const dy = mouse.y - pressPos.y
                const threshold = Qt.styleHints.startDragDistance
                if (dx * dx + dy * dy >= threshold * threshold) {
                    dragStarted = true
                    if (!rootRow.selection.isSelected(rootRow.index)) {
                        rootRow.selection.select(rootRow.index, Qt.NoModifier)
                        rootRow.table.currentIndex = rootRow.index
                    }
                    const ids = rootRow.table.model.trackIds(rootRow.selection.selectedRows())
                    if (ids.length > 0 && rootRow.Window.window && rootRow.Window.window.startTrackDrag) {
                        rootRow.Window.window.startTrackDrag(ids)
                    }
                }
            }
        }

        onReleased: (mouse) => {
            preventStealing = false
            if (dragStarted) {
                dragStarted = false
                return
            }
        }

        onCanceled: () => {
            preventStealing = false
            dragStarted = false
        }

        onClicked: (mouse) => {
            if (dragStarted) return
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
            if (dragStarted) return
            if (mouse.button === Qt.LeftButton) {
                if (AppContext.actions) {
                    AppContext.actions.playTracks(rootRow.table.model.allTrackIds(), rootRow.index, rootRow.table.playSource)
                }
            }
        }
    }
}
