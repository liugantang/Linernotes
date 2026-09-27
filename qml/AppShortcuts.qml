// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import Linernotes

Item {
    id: root
    visible: false

    enum Group {
        Playback,
        Navigation,
        Library,
        General
    }

    property var sidebar: null
    property var sidePanel: null
    property var playerBar: null
    property var shortcutsDialog: null

    readonly property var entries: [
        // Playback
        {
            sequence: "Space",
            description: qsTr("Play / Pause"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (AppContext.player) {
                    AppContext.player.togglePause()
                }
            }
        },
        {
            sequence: "Ctrl+Right",
            description: qsTr("Next track"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (AppContext.player) {
                    AppContext.player.next()
                }
            }
        },
        {
            sequence: "Ctrl+Left",
            description: qsTr("Previous track"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (AppContext.player) {
                    AppContext.player.previous()
                }
            }
        },
        {
            sequence: "Ctrl+Up",
            description: qsTr("Volume up"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (AppContext.player) {
                    const newVol = Math.min(100, AppContext.player.volume + 5)
                    AppContext.player.setVolume(newVol)
                }
            }
        },
        {
            sequence: "Ctrl+Down",
            description: qsTr("Volume down"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (AppContext.player) {
                    const newVol = Math.max(0, AppContext.player.volume - 5)
                    AppContext.player.setVolume(newVol)
                }
            }
        },
        {
            sequence: "Ctrl+M",
            description: qsTr("Mute / Unmute"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (AppContext.player) {
                    AppContext.player.setMuted(!AppContext.player.muted)
                }
            }
        },
        {
            sequence: "Ctrl+Shift+R",
            description: qsTr("Cycle play mode"),
            group: AppShortcuts.Group.Playback,
            action: () => {
                if (root.playerBar) {
                    root.playerBar.cyclePlayMode()
                }
            }
        },

        // Navigation
        {
            sequence: "Alt+1",
            description: qsTr("Go to Tracks"),
            group: AppShortcuts.Group.Navigation,
            action: () => {
                if (root.sidebar) {
                    root.sidebar.currentPage = NavigationSidebar.Tracks
                }
            }
        },
        {
            sequence: "Alt+2",
            description: qsTr("Go to Albums"),
            group: AppShortcuts.Group.Navigation,
            action: () => {
                if (root.sidebar) {
                    root.sidebar.currentPage = NavigationSidebar.Albums
                }
            }
        },
        {
            sequence: "Alt+3",
            description: qsTr("Go to Artists"),
            group: AppShortcuts.Group.Navigation,
            action: () => {
                if (root.sidebar) {
                    root.sidebar.currentPage = NavigationSidebar.Artists
                }
            }
        },
        {
            sequence: "Alt+4",
            description: qsTr("Go to Playlists"),
            group: AppShortcuts.Group.Navigation,
            action: () => {
                if (root.sidebar) {
                    root.sidebar.currentPage = NavigationSidebar.Playlists
                }
            }
        },
        {
            sequence: "Alt+5",
            description: qsTr("Go to AI"),
            group: AppShortcuts.Group.Navigation,
            action: () => {
                if (root.sidebar) {
                    root.sidebar.currentPage = NavigationSidebar.Ai
                }
            }
        },
        {
            sequence: "Ctrl+,",
            description: qsTr("Settings"),
            group: AppShortcuts.Group.Navigation,
            action: () => {
                if (root.sidebar) {
                    root.sidebar.currentPage = NavigationSidebar.Settings
                }
            }
        },
        {
            sequence: "Ctrl+F",
            description: qsTr("Search"),
            group: AppShortcuts.Group.Navigation,
            action: null
        },
        {
            sequence: "Esc",
            description: qsTr("Back from detail page"),
            group: AppShortcuts.Group.Navigation,
            action: null
        },

        // Library
        {
            sequence: "Ctrl+L",
            description: qsTr("Favorite / Unfavorite current track"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.toggleFavorite(Library.FavoriteKind.Track, AppContext.nowPlaying.trackId)
                }
            }
        },
        {
            sequence: "Ctrl+0",
            description: qsTr("Clear rating"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.setRating([AppContext.nowPlaying.trackId], 0)
                }
            }
        },
        {
            sequence: "Ctrl+1",
            description: qsTr("Rate 1 star"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.setRating([AppContext.nowPlaying.trackId], 1)
                }
            }
        },
        {
            sequence: "Ctrl+2",
            description: qsTr("Rate 2 stars"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.setRating([AppContext.nowPlaying.trackId], 2)
                }
            }
        },
        {
            sequence: "Ctrl+3",
            description: qsTr("Rate 3 stars"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.setRating([AppContext.nowPlaying.trackId], 3)
                }
            }
        },
        {
            sequence: "Ctrl+4",
            description: qsTr("Rate 4 stars"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.setRating([AppContext.nowPlaying.trackId], 4)
                }
            }
        },
        {
            sequence: "Ctrl+5",
            description: qsTr("Rate 5 stars"),
            group: AppShortcuts.Group.Library,
            action: () => {
                if (AppContext.marks && AppContext.nowPlaying && AppContext.nowPlaying.hasTrack && AppContext.nowPlaying.trackId >= 0) {
                    AppContext.marks.setRating([AppContext.nowPlaying.trackId], 5)
                }
            }
        },

        // General
        {
            sequence: "Ctrl+Alt+P",
            description: qsTr("Toggle side panel"),
            group: AppShortcuts.Group.General,
            action: () => {
                if (root.sidePanel) {
                    root.sidePanel.collapsed = !root.sidePanel.collapsed
                }
            }
        },
        {
            sequence: "F1",
            description: qsTr("Keyboard shortcuts"),
            group: AppShortcuts.Group.General,
            action: () => {
                if (root.shortcutsDialog) {
                    root.shortcutsDialog.open()
                }
            }
        },
        {
            sequence: "Ctrl+/",
            description: qsTr("Keyboard shortcuts"),
            group: AppShortcuts.Group.General,
            action: () => {
                if (root.shortcutsDialog) {
                    root.shortcutsDialog.open()
                }
            }
        }
    ]

    Instantiator {
        model: root.entries.filter(item => item.action !== null && item.action !== undefined)
        delegate: Shortcut {
            sequence: modelData.sequence
            context: Qt.WindowShortcut
            onActivated: {
                if (typeof modelData.action === "function") {
                    modelData.action()
                }
            }
        }
    }
}
