// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    function refreshAudioDevices() {
        if (AppContext.player) {
            AppContext.player.refreshAudioDevices()
        }
    }

    function updateAudioDeviceModel() {
        const items = [{ text: qsTr("System Default"), value: "" }]
        if (AppContext.player && AppContext.player.audioDevices) {
            const devs = AppContext.player.audioDevices
            for (let i = 0; i < devs.length; ++i) {
                const dev = devs[i]
                items.push({
                    text: dev.description || dev.name || qsTr("Unknown Device"),
                    value: dev.name || ""
                })
            }
        }
        audioDeviceCombo.model = items
        syncAudioDeviceIndex()
    }

    function syncAudioDeviceIndex() {
        if (!AppContext.settings) return
        const currentDev = AppContext.settings.audioDevice
        const idx = audioDeviceCombo.indexOfValue(currentDev)
        if (idx >= 0) {
            audioDeviceCombo.currentIndex = idx
        } else {
            audioDeviceCombo.currentIndex = 0
        }
    }

    function syncReplayGainIndex() {
        if (AppContext.settings) {
            const idx = replayGainCombo.indexOfValue(AppContext.settings.replayGainMode)
            if (idx >= 0 && idx !== replayGainCombo.currentIndex) {
                replayGainCombo.currentIndex = idx
            }
        }
    }

    Component.onCompleted: {
        refreshAudioDevices()
        updateAudioDeviceModel()
        syncReplayGainIndex()
    }

    onVisibleChanged: {
        if (visible) {
            refreshAudioDevices()
        }
    }

    Connections {
        target: AppContext.player
        function onAudioDevicesChanged() {
            root.updateAudioDeviceModel()
        }
    }

    Connections {
        target: AppContext.settings
        function onAudioDeviceChanged() {
            root.syncAudioDeviceIndex()
        }
        function onReplayGainModeChanged() {
            root.syncReplayGainIndex()
        }
    }

    // Section title
    Label {
        text: qsTr("Playback")
        font.pixelSize: Theme.fontSizeLarge
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }

    // ReplayGain
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        Label {
            text: qsTr("ReplayGain")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.text
            Layout.preferredWidth: 140
        }

        Controls.AppComboBox {
            id: replayGainCombo
            Layout.preferredWidth: 200
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("Off"), value: Player.Off },
                { text: qsTr("Track"), value: Player.Track },
                { text: qsTr("Album"), value: Player.Album }
            ]
            onActivated: {
                if (AppContext.settings) {
                    AppContext.settings.replayGainMode = currentValue
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    // Gapless playback
    Controls.AppCheckBox {
        id: gaplessCheck
        text: qsTr("Gapless playback")
        checked: AppContext.settings ? AppContext.settings.gapless : true
        onToggled: {
            if (AppContext.settings) {
                AppContext.settings.gapless = checked
            }
        }

        Connections {
            target: AppContext.settings
            function onGaplessChanged() {
                gaplessCheck.checked = AppContext.settings.gapless
            }
        }
    }

    // Audio Output device
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingMedium

        Label {
            text: qsTr("Output Device")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.text
            Layout.preferredWidth: 140
        }

        Controls.AppComboBox {
            id: audioDeviceCombo
            Layout.preferredWidth: 280
            textRole: "text"
            valueRole: "value"
            onActivated: {
                if (AppContext.settings) {
                    AppContext.settings.audioDevice = currentValue
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    // Exclusive mode
    ColumnLayout {
        spacing: Theme.spacingTiny

        Controls.AppCheckBox {
            id: exclusiveCheck
            text: qsTr("Exclusive mode")
            checked: AppContext.settings ? AppContext.settings.exclusiveMode : false
            onToggled: {
                if (AppContext.settings) {
                    AppContext.settings.exclusiveMode = checked
                }
            }

            Connections {
                target: AppContext.settings
                function onExclusiveModeChanged() {
                    exclusiveCheck.checked = AppContext.settings.exclusiveMode
                }
            }
        }

        Label {
            text: qsTr("Bypass the system mixer (may not be supported by all devices)")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            Layout.leftMargin: 26
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
    }
}
