// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Item {
    id: root

    property alias artistId: albumModel.artistId
    property bool showToolbar: true

    signal albumClicked(var albumId)

    AlbumGridModel {
        id: albumModel
        context: AppContext
    }

    TrackListModel {
        id: helperTrackModel
        context: AppContext
    }

    function playAlbum(albumId) {
        if (!AppContext.actions) {
            return
        }
        helperTrackModel.albumId = albumId
        helperTrackModel.sortKey = TrackListModel.Default
        helperTrackModel.sortOrder = Qt.AscendingOrder
        const ids = helperTrackModel.allTrackIds()
        if (ids.length > 0) {
            AppContext.actions.playTracks(ids, 0)
        }
    }

    function activateCurrent() {
        if (gridView.currentIndex >= 0 && gridView.currentIndex < albumModel.count) {
            const id = albumModel.data(albumModel.index(gridView.currentIndex, 0), AlbumGridModel.AlbumIdRole)
            if (id) {
                root.albumClicked(id)
            }
        }
    }

    function playCurrent() {
        if (gridView.currentIndex >= 0 && gridView.currentIndex < albumModel.count) {
            const id = albumModel.data(albumModel.index(gridView.currentIndex, 0), AlbumGridModel.AlbumIdRole)
            if (id) {
                playAlbum(id)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Toolbar
        RowLayout {
            id: toolbar
            visible: root.showToolbar
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Sort by:")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }

            ComboBox {
                id: sortCombo
                model: [
                    { text: qsTr("Title"), key: AlbumGridModel.Title },
                    { text: qsTr("Artist"), key: AlbumGridModel.Artist },
                    { text: qsTr("Year"), key: AlbumGridModel.Year },
                    { text: qsTr("Date Added"), key: AlbumGridModel.DateAdded }
                ]
                textRole: "text"
                currentIndex: 0
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < model.length) {
                        albumModel.sortKey = model[currentIndex].key
                    }
                }
            }

            Button {
                text: albumModel.sortOrder === Qt.AscendingOrder ? qsTr("Ascending ▲") : qsTr("Descending ▼")
                onClicked: {
                    albumModel.sortOrder = (albumModel.sortOrder === Qt.AscendingOrder)
                        ? Qt.DescendingOrder : Qt.AscendingOrder
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("%n album(s)", "", albumModel.count)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }
        }

        Rectangle {
            visible: root.showToolbar
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                visible: albumModel.count === 0
                text: qsTr("No albums in library")
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.textSecondary
            }

            GridView {
                id: gridView
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                clip: true
                focus: true
                reuseItems: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                model: albumModel

                readonly property int availableWidth: Math.max(100, width)
                readonly property int columns: Math.max(1, Math.floor((availableWidth + Theme.spacingMedium) / (Theme.albumCardMinWidth + Theme.spacingMedium)))
                readonly property int cardWidth: Math.max(80, Math.floor((availableWidth - ((columns - 1) * Theme.spacingMedium)) / columns))

                cellWidth: cardWidth + Theme.spacingMedium
                cellHeight: cardWidth + Theme.albumCardTextHeight + Theme.spacingMedium

                Keys.onReturnPressed: (event) => { activateCurrent(); event.accepted = true; }
                Keys.onEnterPressed: (event) => { activateCurrent(); event.accepted = true; }
                Keys.onSpacePressed: (event) => { playCurrent(); event.accepted = true; }

                delegate: Item {
                    id: cardDelegate
                    required property int index
                    required property var albumId
                    required property string title
                    required property string albumArtist
                    required property string coverHash

                    width: gridView.cardWidth
                    height: gridView.cardWidth + Theme.albumCardTextHeight

                    Rectangle {
                        id: cardBackground
                        anchors.fill: parent
                        radius: Theme.cardBorderRadius
                        color: {
                            if (gridView.currentIndex === cardDelegate.index && gridView.activeFocus) {
                                return Theme.itemSelected
                            }
                            if (cardMouseArea.containsMouse) {
                                return Theme.itemHover
                            }
                            return "transparent"
                        }
                        border.color: gridView.currentIndex === cardDelegate.index && gridView.activeFocus
                            ? Theme.accent : "transparent"
                        border.width: 1
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingTiny
                        spacing: Theme.spacingTiny

                        Item {
                            id: coverContainer
                            Layout.fillWidth: true
                            Layout.preferredHeight: width

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.coverBorderRadius
                                color: Theme.surfaceVariant
                                clip: true

                                Label {
                                    anchors.centerIn: parent
                                    visible: coverImage.status !== Image.Ready || !cardDelegate.coverHash
                                    text: (cardDelegate.title && cardDelegate.title.length > 0)
                                        ? cardDelegate.title.charAt(0).toUpperCase() : "?"
                                    font.pixelSize: Math.max(16, Math.floor(parent.width * 0.35))
                                    font.bold: true
                                    color: Theme.textSecondary
                                }

                                Image {
                                    id: coverImage
                                    anchors.fill: parent
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    source: cardDelegate.coverHash
                                        ? ("image://cover/" + encodeURIComponent(cardDelegate.coverHash)) : ""
                                    sourceSize: Qt.size(gridView.cardWidth, gridView.cardWidth)
                                    visible: status === Image.Ready
                                }
                            }

                            // Hover play button
                            Rectangle {
                                id: playButtonCircle
                                visible: cardMouseArea.containsMouse
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.margins: Theme.spacingSmall
                                width: 36
                                height: 36
                                radius: 18
                                color: Theme.accent

                                Label {
                                    anchors.centerIn: parent
                                    text: "▶"
                                    color: "#ffffff"
                                    font.pixelSize: Theme.fontSizeSmall
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: (event) => {
                                        event.accepted = true
                                        root.playAlbum(cardDelegate.albumId)
                                    }
                                }
                            }
                        }

                        Label {
                            text: cardDelegate.title || qsTr("Unknown Album")
                            font.pixelSize: Theme.fontSizeNormal
                            font.bold: true
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            maximumLineCount: 1
                        }

                        Label {
                            text: cardDelegate.albumArtist || qsTr("Unknown Artist")
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            maximumLineCount: 1
                        }
                    }

                    MouseArea {
                        id: cardMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            gridView.currentIndex = cardDelegate.index
                            root.albumClicked(cardDelegate.albumId)
                        }
                    }
                }
            }
        }
    }
}
