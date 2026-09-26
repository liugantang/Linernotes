// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

RowLayout {
    id: root

    spacing: Theme.spacingSmall

    readonly property bool isMuted: AppContext.player ? AppContext.player.muted : false
    readonly property int currentVolume: AppContext.player ? AppContext.player.volume : 100

    function getVolumeIcon() {
        if (root.isMuted || root.currentVolume === 0) {
            return "icons/volume-x.svg"
        }
        if (root.currentVolume < 50) {
            return "icons/volume-1.svg"
        }
        return "icons/volume-2.svg"
    }

    Controls.IconButton {
        id: muteButton
        icon.source: root.getVolumeIcon()
        toolTip: root.isMuted ? qsTr("Unmute") : qsTr("Mute")
        onClicked: {
            if (AppContext.player) {
                AppContext.player.setMuted(!AppContext.player.muted)
            }
        }
    }

    Controls.SeekBar {
        id: volumeSlider
        Layout.preferredWidth: 100
        from: 0
        to: 100
        stepSize: 1
        playbackPosition: root.currentVolume

        onMoved: {
            if (AppContext.player) {
                AppContext.player.setVolume(Math.round(value))
            }
        }

        onSeekRequested: function(targetValue) {
            if (AppContext.player) {
                AppContext.player.setVolume(Math.round(targetValue))
            }
        }

        WheelHandler {
            target: volumeSlider
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                if (!AppContext.player) {
                    return
                }
                const delta = event.angleDelta.y > 0 ? 5 : -5
                const newVol = Math.max(0, Math.min(100, AppContext.player.volume + delta))
                AppContext.player.setVolume(newVol)
            }
        }
    }
}
