// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import Linernotes
import "controls" as Controls

Controls.AppMenu {
    id: root

    property var trackIds: []

    function popupFor(ids) {
        if (!ids || ids.length === 0) {
            return
        }
        trackIds = ids
        popup()
    }

    function popupAt(ids, item) {
        if (!ids || ids.length === 0 || !item) {
            return
        }
        trackIds = ids
        popup(item, 0, item.height)
    }

    Controls.AppMenuItem {
        text: qsTr("Play")
        onTriggered: {
            if (AppContext.actions && root.trackIds.length > 0) {
                AppContext.actions.playTracks(root.trackIds, 0)
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
        title: qsTr("Add to Playlist")

        Controls.AppMenuItem {
            text: qsTr("(Playlist feature not yet implemented)")
            enabled: false
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
        enabled: false
    }
}
