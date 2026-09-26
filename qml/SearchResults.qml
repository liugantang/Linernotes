// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Rectangle {
    id: root

    color: Theme.background

    signal openAlbumRequested(var albumId)
    signal openArtistRequested(var artistId)
    signal focusSearchBoxRequested()

    function focusTrackList() {
        if (tracksSection.visible) {
            tracksSection.focusList()
        }
    }

    TrackContextMenu {
        id: trackContextMenu
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

    readonly property bool hasResults: AppContext.search && (AppContext.search.tracks.length > 0
        || AppContext.search.albums.length > 0 || AppContext.search.artists.length > 0)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Header
        Label {
            text: qsTr("Search \"%1\"").arg(AppContext.search ? AppContext.search.query : "")
            font.pixelSize: Theme.fontSizeTitle
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }

        // Empty state
        Label {
            visible: !AppContext.search.searching && !root.hasResults
            text: qsTr("No results found for \"%1\"").arg(AppContext.search ? AppContext.search.query : "")
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // Scrollable content
        ScrollView {
            id: scrollView
            visible: root.hasResults
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            ColumnLayout {
                width: scrollView.width - (scrollView.ScrollBar.vertical.visible ? scrollView.ScrollBar.vertical.width : 0)
                spacing: Theme.spacingLarge

                SearchSectionArtists {
                    visible: AppContext.search && AppContext.search.artists.length > 0
                    Layout.fillWidth: true
                    onArtistSelected: (artistId) => {
                        AppContext.search.clear()
                        root.openArtistRequested(artistId)
                    }
                }

                SearchSectionAlbums {
                    visible: AppContext.search && AppContext.search.albums.length > 0
                    Layout.fillWidth: true
                    onAlbumSelected: (albumId) => {
                        AppContext.search.clear()
                        root.openAlbumRequested(albumId)
                    }
                    onPlayAlbumRequested: (albumId) => {
                        AppContext.search.clear()
                        root.playAlbum(albumId)
                    }
                }

                SearchSectionTracks {
                    id: tracksSection
                    visible: AppContext.search && AppContext.search.tracks.length > 0
                    Layout.fillWidth: true
                    onPlayTrackRequested: (index) => {
                        if (AppContext.actions) {
                            AppContext.actions.playTracks(AppContext.search.trackIds(), index)
                        }
                    }
                    onTrackContextMenuRequested: (trackId) => {
                        trackContextMenu.popupFor([trackId])
                    }
                    onTrackContextMenuAtRequested: (trackId, item) => {
                        trackContextMenu.popupAt([trackId], item)
                    }
                    onFocusSearchBoxRequested: {
                        root.focusSearchBoxRequested()
                    }
                }
            }
        }
    }
}
