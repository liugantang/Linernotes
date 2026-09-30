// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import Linernotes
import "controls" as Controls

Controls.AppMenu {
    id: root

    property var trackIds: []
    property var playlistId: 0
    property int playSource: PlaySource.Library
    property var otherVersionsList: []

    function updateOtherVersions(ids) {
        if (ids && ids.length === 1 && AppContext.actions) {
            otherVersionsList = AppContext.actions.otherVersions(ids[0])
        } else {
            otherVersionsList = []
        }
    }

    function versionLabelText(vt) {
        if (vt === Library.VersionType.Live) return qsTr("Live")
        if (vt === Library.VersionType.Remaster) return qsTr("Remaster")
        if (vt === Library.VersionType.Acoustic) return qsTr("Acoustic")
        if (vt === Library.VersionType.Remix) return qsTr("Remix")
        if (vt === Library.VersionType.Demo) return qsTr("Demo")
        if (vt === Library.VersionType.Instrumental) return qsTr("Instrumental")
        if (vt === Library.VersionType.Edit) return qsTr("Edit")
        if (vt === Library.VersionType.Alternate) return qsTr("Alt. version")
        return ""
    }

    function formatVersionItemText(item) {
        const parts = []
        if (item.title) {
            parts.push(item.title)
        } else {
            parts.push(qsTr("Unknown Title"))
        }
        const vText = versionLabelText(item.versionType)
        if (vText) {
            parts.push(vText)
        }
        if (item.durationText) {
            parts.push(item.durationText)
        }
        if (item.album) {
            parts.push(item.album)
        }
        return parts.join(" · ")
    }

    function popupFor(ids) {
        if (!ids || ids.length === 0) {
            return
        }
        trackIds = ids
        updateOtherVersions(ids)
        popup()
    }

    function popupAt(ids, item) {
        if (!ids || ids.length === 0 || !item) {
            return
        }
        trackIds = ids
        updateOtherVersions(ids)
        popup(item, 0, item.height)
    }

    PlaylistNameDialog {
        id: newPlaylistDialog
        titleText: qsTr("New Playlist")
        acceptButtonText: qsTr("Create")
        onAccepted: (name) => {
            if (AppContext.playlists && root.trackIds.length > 0) {
                AppContext.playlists.createManual(name, root.trackIds)
            }
        }
    }

    TagEditorDialog {
        id: tagEditorDialog
    }

    Controls.AppMenuItem {
        text: qsTr("Play")
        onTriggered: {
            if (AppContext.actions && root.trackIds.length > 0) {
                AppContext.actions.playTracks(root.trackIds, 0, root.playSource)
            }
        }
    }

    Controls.AppMenuItem {
        text: qsTr("Play Next")
        onTriggered: {
            if (AppContext.actions && root.trackIds.length > 0) {
                AppContext.actions.playNext(root.trackIds)
            }
        }
    }

    Controls.AppMenuItem {
        text: qsTr("Add to Queue")
        onTriggered: {
            if (AppContext.actions && root.trackIds.length > 0) {
                AppContext.actions.enqueue(root.trackIds)
            }
        }
    }

    Controls.AppMenu {
        id: addToPlaylistMenu
        title: qsTr("Add to Playlist")

        Controls.AppMenuItem {
            text: qsTr("New Playlist...")
            onTriggered: {
                newPlaylistDialog.openWithText("")
            }
        }

        Controls.AppMenuSeparator {
            id: playlistSep
            visible: addToPlaylistMenu.count > 2
            height: visible ? implicitHeight : 0
        }

        Instantiator {
            id: playlistInstantiator
            model: AppContext.playlists ? AppContext.playlists.model : null
            delegate: Controls.AppMenuItem {
                required property var model
                readonly property bool isSmart: model.isSmart
                text: model.name
                onTriggered: {
                    if (AppContext.playlists && root.trackIds.length > 0) {
                        AppContext.playlists.addTracks(model.playlistId, root.trackIds)
                    }
                }
            }
            onObjectAdded: (index, object) => {
                if (!object.isSmart) {
                    addToPlaylistMenu.addItem(object)
                }
            }
            onObjectRemoved: (index, object) => {
                if (!object.isSmart) {
                    addToPlaylistMenu.removeItem(object)
                }
            }
        }
    }

    Controls.AppMenu {
        id: otherVersionsMenu
        title: qsTr("Other Versions")
        visible: root.trackIds.length === 1 && root.otherVersionsList.length > 0
        height: visible ? implicitHeight : 0

        Instantiator {
            id: otherVersionsInstantiator
            model: root.otherVersionsList
            delegate: Controls.AppMenuItem {
                required property var modelData
                text: root.formatVersionItemText(modelData)
                onTriggered: {
                    if (AppContext.actions && modelData && modelData.trackId) {
                        AppContext.actions.playTracks([modelData.trackId], 0, root.playSource)
                    }
                }
            }
            onObjectAdded: (index, object) => {
                otherVersionsMenu.addItem(object)
            }
            onObjectRemoved: (index, object) => {
                otherVersionsMenu.removeItem(object)
            }
        }
    }

    Controls.AppMenuItem {
        text: qsTr("Remove from Playlist")
        visible: root.playlistId > 0 && AppContext.playlists && AppContext.playlists.isManual(root.playlistId)
        height: visible ? implicitHeight : 0
        onTriggered: {
            if (AppContext.playlists && root.playlistId > 0 && root.trackIds.length > 0) {
                AppContext.playlists.removeTracks(root.playlistId, root.trackIds)
            }
        }
    }

    Controls.AppMenuSeparator {}

    Controls.AppMenuItem {
        readonly property bool isCurrentFavorite: (AppContext.marks && root.trackIds.length > 0)
            ? AppContext.marks.isFavorite(Library.FavoriteKind.Track, root.trackIds[0]) : false
        text: isCurrentFavorite ? qsTr("Remove from Loved") : qsTr("Love")
        onTriggered: {
            if (AppContext.marks && root.trackIds.length > 0) {
                AppContext.marks.setFavorite(Library.FavoriteKind.Track, root.trackIds, !isCurrentFavorite)
            }
        }
    }

    Controls.AppMenu {
        id: ratingMenu
        title: qsTr("Rating")

        Controls.AppMenuItem {
            text: qsTr("None")
            onTriggered: {
                if (AppContext.marks && root.trackIds.length > 0) {
                    AppContext.marks.setRating(root.trackIds, 0)
                }
            }
        }

        Controls.AppMenuItem {
            text: "★"
            onTriggered: {
                if (AppContext.marks && root.trackIds.length > 0) {
                    AppContext.marks.setRating(root.trackIds, 1)
                }
            }
        }

        Controls.AppMenuItem {
            text: "★★"
            onTriggered: {
                if (AppContext.marks && root.trackIds.length > 0) {
                    AppContext.marks.setRating(root.trackIds, 2)
                }
            }
        }

        Controls.AppMenuItem {
            text: "★★★"
            onTriggered: {
                if (AppContext.marks && root.trackIds.length > 0) {
                    AppContext.marks.setRating(root.trackIds, 3)
                }
            }
        }

        Controls.AppMenuItem {
            text: "★★★★"
            onTriggered: {
                if (AppContext.marks && root.trackIds.length > 0) {
                    AppContext.marks.setRating(root.trackIds, 4)
                }
            }
        }

        Controls.AppMenuItem {
            text: "★★★★★"
            onTriggered: {
                if (AppContext.marks && root.trackIds.length > 0) {
                    AppContext.marks.setRating(root.trackIds, 5)
                }
            }
        }
    }

    Controls.AppMenuSeparator {}

    Controls.AppMenuItem {
        text: qsTr("Show in File Manager")
        enabled: root.trackIds.length === 1
        onTriggered: {
            if (AppContext.actions && root.trackIds.length === 1) {
                AppContext.actions.showInFileManager(root.trackIds[0])
            }
        }
    }

    Controls.AppMenuItem {
        text: qsTr("Edit Tags...")
        enabled: root.trackIds.length > 0
        onTriggered: {
            tagEditorDialog.openForTracks(root.trackIds)
        }
    }
}
