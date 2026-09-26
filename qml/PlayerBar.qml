// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Rectangle {
    id: root

    color: Theme.surface
    border.color: Theme.divider
    border.width: 1

    signal openAlbumRequested(var albumId)

    readonly property bool hasTrack: AppContext.nowPlaying ? AppContext.nowPlaying.hasTrack : false
    readonly property bool isPlaying: AppContext.player ? AppContext.player.state === Player.Playing : false

    function cyclePlayMode() {
        if (!AppContext.player || !AppContext.player.queue) {
            return
        }
        const current = AppContext.player.queue.mode
        if (current === PlayMode.Sequential) {
            AppContext.player.queue.mode = PlayMode.RepeatAll
        } else if (current === PlayMode.RepeatAll) {
            AppContext.player.queue.mode = PlayMode.RepeatOne
        } else if (current === PlayMode.RepeatOne) {
            AppContext.player.queue.mode = PlayMode.Shuffle
        } else {
            AppContext.player.queue.mode = PlayMode.Sequential
        }
    }

    function getModeIcon(mode) {
        if (mode === PlayMode.RepeatAll) {
            return "icons/repeat.svg"
        }
        if (mode === PlayMode.RepeatOne) {
            return "icons/repeat-1.svg"
        }
        if (mode === PlayMode.Shuffle) {
            return "icons/shuffle.svg"
        }
        return "icons/arrow-right.svg"
    }

    function getModeToolTip(mode) {
        if (mode === PlayMode.RepeatAll) {
            return qsTr("Repeat All")
        }
        if (mode === PlayMode.RepeatOne) {
            return qsTr("Repeat One")
        }
        if (mode === PlayMode.Shuffle) {
            return qsTr("Shuffle")
        }
        return qsTr("Sequential")
    }

    function isModeActive(mode) {
        return mode !== PlayMode.Sequential
    }

    function formatDuration(seconds) {
        if (isNaN(seconds) || seconds <= 0) {
            return "0:00"
        }
        const totalSecs = Math.floor(seconds)
        const hrs = Math.floor(totalSecs / 3600)
        const mins = Math.floor((totalSecs % 3600) / 60)
        const secs = totalSecs % 60
        const secStr = secs < 10 ? "0" + secs : "" + secs
        if (hrs > 0) {
            const minStr = mins < 10 ? "0" + mins : "" + mins
            return hrs + ":" + minStr + ":" + secStr
        }
        return mins + ":" + secStr
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Left Section: Track Info (Cover + Title + Artist)
        PlayerBarTrackInfo {
            id: trackInfo
            Layout.preferredWidth: Math.max(220, Math.min(360, Math.floor(root.width * 0.25)))
            Layout.minimumWidth: 220
            Layout.maximumWidth: Math.max(220, Math.min(360, Math.floor(root.width * 0.25)))
            Layout.fillHeight: true

            onOpenAlbumRequested: function(albumId) {
                root.openAlbumRequested(albumId)
            }
        }

        // Middle Section: Playback Controls & Progress Bar
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingTiny

            // Control Buttons (Centered)
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacingSmall

                Controls.IconButton {
                    id: modeButton
                    icon.source: root.getModeIcon(AppContext.player && AppContext.player.queue ? AppContext.player.queue.mode : -1)
                    icon.color: root.isModeActive(AppContext.player && AppContext.player.queue ? AppContext.player.queue.mode : -1) ? Theme.accent : Theme.text
                    toolTip: root.getModeToolTip(AppContext.player && AppContext.player.queue ? AppContext.player.queue.mode : -1)
                    onClicked: root.cyclePlayMode()
                }

                Controls.IconButton {
                    icon.source: "icons/skip-back.svg"
                    toolTip: qsTr("Previous")
                    enabled: root.hasTrack
                    onClicked: {
                        if (AppContext.player) {
                            AppContext.player.previous()
                        }
                    }
                }

                Controls.IconButton {
                    id: playPauseButton
                    icon.source: root.isPlaying ? "icons/pause.svg" : "icons/play.svg"
                    toolTip: root.isPlaying ? qsTr("Pause") : qsTr("Play")
                    enabled: root.hasTrack
                    implicitWidth: Theme.controlHeight * 1.2
                    implicitHeight: Theme.controlHeight * 1.2
                    icon.width: Theme.iconSize * 1.2
                    icon.height: Theme.iconSize * 1.2
                    icon.color: Theme.accentText

                    background: Rectangle {
                        color: !parent.enabled ? Theme.surfaceVariant : (parent.down ? Theme.accentHover : (parent.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent))
                        radius: width / 2

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -2
                            color: "transparent"
                            border.color: parent.parent.visualFocus ? Theme.focusRing : "transparent"
                            border.width: 2
                            radius: width / 2
                            visible: parent.parent.visualFocus
                        }
                    }

                    onClicked: {
                        if (AppContext.player) {
                            AppContext.player.togglePause()
                        }
                    }
                }

                Controls.IconButton {
                    icon.source: "icons/skip-forward.svg"
                    toolTip: qsTr("Next")
                    enabled: root.hasTrack
                    onClicked: {
                        if (AppContext.player) {
                            AppContext.player.next()
                        }
                    }
                }

                // Symmetrical placeholder to keep play/pause centered
                Item {
                    implicitWidth: modeButton.implicitWidth
                    implicitHeight: modeButton.implicitHeight
                }
            }

            // Seek Bar Row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    id: currentTimeLabel
                    text: root.formatDuration(seekBar.pressed ? seekBar.value : (AppContext.player ? AppContext.player.position : 0))
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 45
                }

                Controls.SeekBar {
                    id: seekBar
                    Layout.fillWidth: true
                    enabled: root.hasTrack
                    from: 0.0
                    to: {
                        const playerDur = AppContext.player ? AppContext.player.duration : 0
                        if (playerDur > 0) {
                            return playerDur
                        }
                        const metaDur = AppContext.nowPlaying ? AppContext.nowPlaying.durationSeconds : 0
                        return metaDur > 0 ? metaDur : 0
                    }
                    playbackPosition: AppContext.player ? AppContext.player.position : 0.0

                    onSeekRequested: function(targetSeconds) {
                        if (AppContext.player) {
                            AppContext.player.seek(targetSeconds)
                        }
                    }
                }

                Label {
                    id: totalTimeLabel
                    text: root.formatDuration(seekBar.to)
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignLeft
                    Layout.preferredWidth: 45
                }
            }
        }

        // Right Section: Volume Control
        RowLayout {
            Layout.preferredWidth: Math.max(200, Math.min(360, Math.floor(root.width * 0.25)))
            Layout.maximumWidth: Math.max(200, Math.min(360, Math.floor(root.width * 0.25)))
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
            }

            VolumeControl {
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
            }
        }
    }
}
