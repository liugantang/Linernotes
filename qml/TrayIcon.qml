// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import Qt.labs.platform as Platform
import Linernotes

Platform.SystemTrayIcon {
    id: root

    property var window: null

    visible: AppContext.settings ? AppContext.settings.trayIcon : true
    icon.source: "qrc:/qt/qml/Linernotes/icons/music.svg"
    tooltip: {
        if (AppContext.nowPlaying && AppContext.nowPlaying.hasTrack) {
            const title = AppContext.nowPlaying.title
            const artist = AppContext.nowPlaying.artist
            if (title && artist) {
                return title + " — " + artist
            }
            if (title) {
                return title
            }
        }
        return "Linernotes"
    }

    onActivated: (reason) => {
        if (reason === Platform.SystemTrayIcon.Trigger) {
            toggleWindowVisibility()
        }
    }

    function toggleWindowVisibility() {
        if (!root.window) {
            return
        }
        if (root.window.visible) {
            root.window.hide()
        } else {
            root.window.show()
            root.window.raise()
            root.window.requestActivate()
        }
    }

    menu: Platform.Menu {
        Platform.MenuItem {
            text: (AppContext.player && AppContext.player.state === Player.Playing) ? qsTr("Pause") : qsTr("Play")
            onTriggered: {
                if (AppContext.player) {
                    AppContext.player.togglePause()
                }
            }
        }

        Platform.MenuItem {
            text: qsTr("Previous")
            onTriggered: {
                if (AppContext.player) {
                    AppContext.player.previous()
                }
            }
        }

        Platform.MenuItem {
            text: qsTr("Next")
            onTriggered: {
                if (AppContext.player) {
                    AppContext.player.next()
                }
            }
        }

        Platform.MenuSeparator {}

        Platform.MenuItem {
            text: (root.window && root.window.visible) ? qsTr("Hide Window") : qsTr("Show Window")
            onTriggered: root.toggleWindowVisibility()
        }

        Platform.MenuItem {
            text: qsTr("Quit")
            onTriggered: Qt.quit()
        }
    }
}
