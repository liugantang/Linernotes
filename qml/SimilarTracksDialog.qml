// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay

    x: Math.round(((Overlay.overlay ? Overlay.overlay.width : 800) - width) / 2)
    y: Math.max(20, Math.round((Overlay.overlay ? Overlay.overlay.height : 600) * 0.08))
    width: Math.min(680, Overlay.overlay ? Overlay.overlay.width - 40 : 680)
    implicitHeight: Math.min(
        Overlay.overlay ? Overlay.overlay.height * 0.85 : 560,
        mainLayout.implicitHeight + topPadding + bottomPadding
    )

    padding: Theme.spacingMedium

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.divider
        border.width: 1
        radius: Theme.cardBorderRadius
    }

    Connections {
        target: (typeof AppContext !== "undefined" && AppContext) ? AppContext.similar : null
        function onOpenRequested() {
            root.open()
            resultListView.currentIndex = 0
            resultListView.forceActiveFocus()
        }
    }

    PlaylistNameDialog {
        id: savePlaylistDialog
        titleText: qsTr("Save as Playlist")
        acceptButtonText: qsTr("Save")
        onAccepted: (name) => {
            if (AppContext.similar) {
                const res = AppContext.similar.saveAsPlaylist(name)
                if (res > 0) {
                    root.close()
                }
            }
        }
    }

    contentItem: ColumnLayout {
        id: mainLayout
        spacing: Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                text: (AppContext.similar && AppContext.similar.playlistMode)
                    ? qsTr("Playlist like “%1”").arg((AppContext.similar && AppContext.similar.seedTitle) ? AppContext.similar.seedTitle : "")
                    : qsTr("Similar to “%1”").arg((AppContext.similar && AppContext.similar.seedTitle) ? AppContext.similar.seedTitle : "")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Controls.IconButton {
                icon.source: "qrc:/qt/qml/Linernotes/icons/x.svg"
                toolTip: qsTr("Close")
                onClicked: root.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Center / Results area
        Item {
            id: contentContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160
            Layout.preferredHeight: 360

            // Unanalyzed prompt
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacingLarge * 2, 480)
                spacing: Theme.spacingMedium
                visible: !!(AppContext.similar && !AppContext.similar.seedAnalyzed)

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("This track has not been analyzed for audio features yet. Go to Settings → Library → Audio Analysis to run analysis and try again.")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                }
            }

            // Analyzed but no similar tracks found
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacingLarge * 2, 480)
                spacing: Theme.spacingMedium
                visible: !!(AppContext.similar && AppContext.similar.seedAnalyzed && AppContext.similar.rows.length === 0)

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: (AppContext.similar && AppContext.similar.playlistMode)
                        ? qsTr("No tracks to recommend.")
                        : qsTr("No similar tracks found.")
                    font.pixelSize: Theme.fontSizeLarge
                    color: Theme.textSecondary
                }
            }

            // Results List
            ListView {
                id: resultListView
                anchors.fill: parent
                visible: !!(AppContext.similar && AppContext.similar.seedAnalyzed && AppContext.similar.rows.length > 0)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                focus: true
                keyNavigationEnabled: true
                model: (AppContext.similar && AppContext.similar.seedAnalyzed) ? AppContext.similar.rows : []

                ScrollBar.vertical: Controls.AppScrollBar {}

                Keys.onReturnPressed: (event) => {
                    if (currentIndex >= 0 && AppContext.similar) {
                        AppContext.similar.playRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: (event) => {
                    if (currentIndex >= 0 && AppContext.similar) {
                        AppContext.similar.playRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onEscapePressed: (event) => {
                    root.close()
                    event.accepted = true
                }

                delegate: Rectangle {
                    id: rowDelegate
                    required property int index
                    required property var modelData

                    width: resultListView.width - (resultListView.ScrollBar.vertical.visible ? resultListView.ScrollBar.vertical.width : 0)
                    height: Theme.trackRowHeight
                    radius: Theme.radiusSmall
                    color: {
                        if (resultListView.currentIndex === index && resultListView.activeFocus) {
                            return Theme.itemSelected
                        }
                        if (rowMouseArea.containsMouse) {
                            return Theme.itemHover
                        }
                        return "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: Theme.radiusSmall
                            color: Theme.surfaceVariant
                            clip: true

                            Image {
                                anchors.fill: parent
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                source: (rowDelegate.modelData && rowDelegate.modelData.coverHash)
                                    ? ("image://cover/" + encodeURIComponent(rowDelegate.modelData.coverHash)) : ""
                                sourceSize: Qt.size(32, 32)
                                visible: status === Image.Ready
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: rowDelegate.modelData ? (rowDelegate.modelData.title || qsTr("Unknown Title")) : ""
                                font.pixelSize: Theme.fontSizeNormal
                                font.bold: true
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: {
                                    if (!rowDelegate.modelData) return ""
                                    const artist = rowDelegate.modelData.artist || qsTr("Unknown Artist")
                                    const album = rowDelegate.modelData.album || ""
                                    return album.length > 0 ? (artist + " · " + album) : artist
                                }
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: rowDelegate.modelData ? (rowDelegate.modelData.durationText || "0:00") : "0:00"
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 50
                        }
                    }

                    MouseArea {
                        id: rowMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            resultListView.currentIndex = rowDelegate.index
                            resultListView.forceActiveFocus()
                        }
                        onDoubleClicked: {
                            if (AppContext.similar) {
                                AppContext.similar.playRow(rowDelegate.index)
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Bottom Action Buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Controls.AppButton {
                text: qsTr("Play All")
                primary: true
                enabled: !!(AppContext.similar && AppContext.similar.seedAnalyzed && AppContext.similar.rows.length > 0)
                onClicked: {
                    if (AppContext.similar) {
                        AppContext.similar.playAll()
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Add to Queue")
                enabled: !!(AppContext.similar && AppContext.similar.seedAnalyzed && AppContext.similar.rows.length > 0)
                onClicked: {
                    if (AppContext.similar) {
                        AppContext.similar.enqueueAll()
                    }
                }
            }

            Controls.AppButton {
                visible: !!(AppContext.similar && AppContext.similar.playlistMode)
                text: qsTr("Save as Playlist")
                enabled: !!(AppContext.similar && AppContext.similar.rows.length > 0)
                onClicked: {
                    const defaultName = qsTr("Playlist like “%1”").arg((AppContext.similar && AppContext.similar.seedTitle) ? AppContext.similar.seedTitle : "")
                    savePlaylistDialog.openWithText(defaultName)
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }
}
