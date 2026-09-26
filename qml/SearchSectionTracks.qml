// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root

    signal playTrackRequested(int index)
    signal trackContextMenuRequested(var trackId)
    signal trackContextMenuAtRequested(var trackId, var item)
    signal focusSearchBoxRequested()

    property bool showAll: false

    function focusList() {
        trackListView.forceActiveFocus()
        if (trackListView.currentIndex < 0) {
            trackListView.currentIndex = 0
        }
    }

    Connections {
        target: AppContext.search
        function onQueryChanged() {
            root.showAll = false
        }
    }

    spacing: Theme.spacingSmall

    readonly property int totalTracks: AppContext.search ? AppContext.search.tracks.length : 0
    readonly property int visibleTrackCount: root.showAll ? totalTracks : Math.min(20, totalTracks)

    Label {
        text: qsTr("Tracks")
        font.pixelSize: Theme.fontSizeLarge
        font.bold: true
        color: Theme.text
    }

    ListView {
        id: trackListView
        Layout.fillWidth: true
        implicitHeight: visibleTrackCount * Theme.trackRowHeight
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        clip: true
        focus: true
        keyNavigationEnabled: true
        model: visibleTrackCount

        Keys.onReturnPressed: (event) => {
            if (currentIndex >= 0 && currentIndex < totalTracks) {
                root.playTrackRequested(currentIndex)
                event.accepted = true
            }
        }
        Keys.onEnterPressed: (event) => {
            if (currentIndex >= 0 && currentIndex < totalTracks) {
                root.playTrackRequested(currentIndex)
                event.accepted = true
            }
        }
        Keys.onUpPressed: (event) => {
            if (currentIndex === 0) {
                root.focusSearchBoxRequested()
                event.accepted = true
            } else {
                event.accepted = false
            }
        }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                if (currentIndex >= 0 && currentIndex < totalTracks && AppContext.search && currentIndex < AppContext.search.tracks.length) {
                    const track = AppContext.search.tracks[currentIndex]
                    if (track && track.trackId) {
                        root.trackContextMenuAtRequested(track.trackId, currentItem || trackListView)
                    }
                }
                event.accepted = true
            }
        }

        delegate: Rectangle {
            id: trackRowDelegate
            required property int index

            readonly property var trackData: (AppContext.search && index < AppContext.search.tracks.length)
                ? AppContext.search.tracks[index] : null

            width: trackListView.width
            height: Theme.trackRowHeight
            radius: Theme.radiusSmall
            color: {
                if (trackListView.currentIndex === index && trackListView.activeFocus) {
                    return Theme.itemSelected
                }
                if (trackMouseArea.containsMouse) {
                    return Theme.itemHover
                }
                return "transparent"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingMedium

                // Track cover
                Rectangle {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    radius: 4
                    color: Theme.surfaceVariant
                    clip: true

                    Image {
                        anchors.fill: parent
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        source: (trackRowDelegate.trackData && trackRowDelegate.trackData.coverHash)
                            ? ("image://cover/" + encodeURIComponent(trackRowDelegate.trackData.coverHash)) : ""
                        sourceSize: Qt.size(32, 32)
                        visible: status === Image.Ready
                    }
                }

                // Title and Artist
                ColumnLayout {
                    Layout.preferredWidth: 260
                    Layout.fillWidth: true
                    spacing: 1

                    Label {
                        text: trackRowDelegate.trackData ? (trackRowDelegate.trackData.title || qsTr("Unknown Title")) : ""
                        font.pixelSize: Theme.fontSizeNormal
                        font.bold: true
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: trackRowDelegate.trackData ? (trackRowDelegate.trackData.artist || qsTr("Unknown Artist")) : ""
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                // Album name
                Label {
                    text: trackRowDelegate.trackData ? (trackRowDelegate.trackData.album || "") : ""
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.preferredWidth: 180
                    Layout.fillWidth: true
                }

                // Duration
                Label {
                    text: trackRowDelegate.trackData ? (trackRowDelegate.trackData.durationText || "0:00") : "0:00"
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignRight
                    Layout.preferredWidth: 50
                }
            }

            MouseArea {
                id: trackMouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (mouse) => {
                    trackListView.currentIndex = trackRowDelegate.index
                    trackListView.forceActiveFocus()
                    if (mouse.button === Qt.RightButton && trackRowDelegate.trackData) {
                        root.trackContextMenuRequested(trackRowDelegate.trackData.trackId)
                    }
                }
                onDoubleClicked: (mouse) => {
                    if (mouse.button === Qt.LeftButton) {
                        root.playTrackRequested(trackRowDelegate.index)
                    }
                }
            }
        }
    }

    // Show all button
    Controls.AppButton {
        visible: !root.showAll && totalTracks > 20
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("Show all %1 tracks").arg(totalTracks)
        onClicked: {
            root.showAll = true
        }
    }
}
