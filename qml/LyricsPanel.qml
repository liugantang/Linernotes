// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    readonly property bool hasTrack: !!AppContext.nowPlaying && AppContext.nowPlaying.hasTrack
    readonly property bool isLoading: !!AppContext.lyrics && AppContext.lyrics.loading
    readonly property int lyricsStatus: AppContext.lyrics ? AppContext.lyrics.status : Guide.LyricsStatus.NotFound
    readonly property bool isSynced: !!AppContext.lyrics && AppContext.lyrics.synced
    readonly property int currentLineIndex: AppContext.lyrics ? AppContext.lyrics.currentIndex : -1

    Timer {
        id: manualScrollTimer
        interval: 3000
        repeat: false
    }

    LyricsEditDialog {
        id: editDialog
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Import Lyrics File")
        nameFilters: [qsTr("Lyrics (*.lrc *.txt)"), qsTr("All files (*)")]
        onAccepted: {
            if (AppContext.lyrics && selectedFile) {
                AppContext.lyrics.importFile(selectedFile)
            }
        }
    }

    Controls.AppMenu {
        id: moreMenu

        Controls.AppMenuItem {
            text: qsTr("Edit Lyrics…")
            enabled: root.hasTrack
            onTriggered: {
                editDialog.openWithText(AppContext.lyrics ? AppContext.lyrics.rawText : "")
            }
        }

        Controls.AppMenuItem {
            text: qsTr("Import .lrc File…")
            enabled: root.hasTrack
            onTriggered: fileDialog.open()
        }

        Controls.AppMenuItem {
            text: qsTr("Search Online Again")
            enabled: root.hasTrack
            onTriggered: {
                if (AppContext.lyrics) {
                    AppContext.lyrics.refetch()
                }
            }
        }
    }

    function followCurrentLine() {
        if (root.isSynced && !manualScrollTimer.running && listView.visible && listView.count > 0) {
            const idx = root.currentLineIndex
            if (idx >= 0 && idx < listView.count) {
                listView.positionViewAtIndex(idx, ListView.Center)
            }
        }
    }

    // 标签页切回来、歌词刚载入时也要滚到当前行（此时 currentIndex 不一定变化）
    onVisibleChanged: Qt.callLater(root.followCurrentLine)

    Connections {
        target: AppContext.lyrics
        function onCurrentIndexChanged() {
            root.followCurrentLine()
        }
        function onLinesChanged() {
            Qt.callLater(root.followCurrentLine)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Main content area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 1. Nothing playing state
            Item {
                anchors.centerIn: parent
                visible: !root.hasTrack

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Label {
                        text: qsTr("Nothing playing.")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // 2. Loading state
            Item {
                anchors.centerIn: parent
                visible: root.hasTrack && root.isLoading

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Label {
                        text: qsTr("Looking for lyrics…")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // 3. Instrumental state
            Item {
                anchors.centerIn: parent
                visible: root.hasTrack && !root.isLoading && root.lyricsStatus === Guide.LyricsStatus.Instrumental

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Label {
                        text: qsTr("Instrumental")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // 4. Failed state with retry
            Item {
                anchors.centerIn: parent
                visible: root.hasTrack && !root.isLoading && root.lyricsStatus === Guide.LyricsStatus.Failed

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingMedium

                    Label {
                        text: qsTr("Couldn't fetch lyrics.")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Controls.AppButton {
                        text: qsTr("Retry")
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: {
                            if (AppContext.lyrics) {
                                AppContext.lyrics.refetch()
                            }
                        }
                    }
                }
            }

            // 5. Not found state
            Item {
                anchors.centerIn: parent
                visible: root.hasTrack && !root.isLoading && (root.lyricsStatus === Guide.LyricsStatus.NotFound || (root.lyricsStatus === Guide.LyricsStatus.Found && (!AppContext.lyrics || AppContext.lyrics.lines.length === 0)))

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSmall

                    Label {
                        text: qsTr("No lyrics found.")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // 6. Lyrics ListView
            ListView {
                id: listView
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                visible: root.hasTrack && !root.isLoading && root.lyricsStatus === Guide.LyricsStatus.Found && AppContext.lyrics && AppContext.lyrics.lines.length > 0
                model: AppContext.lyrics ? AppContext.lyrics.lines : []

                onMovingChanged: {
                    if (moving) {
                        manualScrollTimer.restart()
                    }
                }
                onFlickingChanged: {
                    if (flicking) {
                        manualScrollTimer.restart()
                    }
                }
                onDraggingChanged: {
                    if (dragging) {
                        manualScrollTimer.restart()
                    }
                }

                ScrollBar.vertical: Controls.AppScrollBar {}

                delegate: Item {
                    id: lineDelegate
                    required property int index
                    required property var modelData

                    readonly property bool isCurrent: root.isSynced && root.currentLineIndex === lineDelegate.index

                    width: listView.width
                    implicitHeight: lineCol.implicitHeight + Theme.spacingSmall * 2

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingSmall
                        radius: Theme.radiusSmall
                        color: (lineMouseArea.containsMouse && root.isSynced)
                            ? Theme.itemHover : "transparent"
                    }

                    ColumnLayout {
                        id: lineCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingTiny

                        Label {
                            Layout.fillWidth: true
                            text: (lineDelegate.modelData && lineDelegate.modelData.text)
                                ? lineDelegate.modelData.text : ""
                            font.pixelSize: lineDelegate.isCurrent ? (Theme.fontSizeNormal + 2) : Theme.fontSizeNormal
                            font.bold: lineDelegate.isCurrent
                            color: lineDelegate.isCurrent ? Theme.text : Theme.textSecondary
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            visible: lineDelegate.modelData && !!lineDelegate.modelData.translation && lineDelegate.modelData.translation.length > 0
                            Layout.fillWidth: true
                            text: (lineDelegate.modelData && lineDelegate.modelData.translation) ? lineDelegate.modelData.translation : ""
                            font.pixelSize: lineDelegate.isCurrent ? Theme.fontSizeNormal : Theme.fontSizeSmall
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: lineMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: root.isSynced ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            if (root.isSynced && AppContext.lyrics) {
                                AppContext.lyrics.seekToLine(lineDelegate.index)
                                manualScrollTimer.stop()
                                listView.positionViewAtIndex(lineDelegate.index, ListView.Center)
                            }
                        }
                    }
                }
            }
        }

        // Bottom separator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Bottom status & source bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight + Theme.spacingSmall
            color: Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                // Source indicator or translation error
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.spacingTiny

                        // Source indicator (shown when no translation error)
                        Label {
                            text: qsTr("Source:")
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            visible: root.hasTrack && root.lyricsStatus === Guide.LyricsStatus.Found && !!AppContext.lyrics && (!AppContext.lyrics.translationError || AppContext.lyrics.translationError.length === 0)
                        }

                        // LRCLIB Link
                        Text {
                            visible: root.hasTrack && root.lyricsStatus === Guide.LyricsStatus.Found && !!AppContext.lyrics && AppContext.lyrics.source === Guide.LyricsSource.Lrclib && (!AppContext.lyrics.translationError || AppContext.lyrics.translationError.length === 0)
                            text: "LRCLIB"
                            font.pixelSize: Theme.fontSizeSmall
                            color: lrclibMouse.containsMouse ? Theme.accentHover : Theme.accent
                            font.underline: true

                            MouseArea {
                                id: lrclibMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally("https://lrclib.net")
                            }
                        }

                        // Other sources
                        Label {
                            visible: root.hasTrack && root.lyricsStatus === Guide.LyricsStatus.Found && !!AppContext.lyrics && AppContext.lyrics.source !== Guide.LyricsSource.Lrclib && (!AppContext.lyrics.translationError || AppContext.lyrics.translationError.length === 0)
                            text: {
                                if (!AppContext.lyrics) return ""
                                switch (AppContext.lyrics.source) {
                                case Guide.LyricsSource.Manual:
                                    return qsTr("Edited by you")
                                case Guide.LyricsSource.Embedded:
                                    return qsTr("Embedded tags")
                                case Guide.LyricsSource.LrcFile:
                                    return qsTr(".lrc file")
                                default:
                                    return ""
                                }
                            }
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                        }

                        // Translation error
                        Label {
                            visible: root.hasTrack && root.lyricsStatus === Guide.LyricsStatus.Found && !!AppContext.lyrics && !!AppContext.lyrics.translationError && AppContext.lyrics.translationError.length > 0
                            text: AppContext.lyrics ? AppContext.lyrics.translationError : ""
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.errorText
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            ToolTip.visible: errorMouse.containsMouse && text.length > 0
                            ToolTip.text: text

                            MouseArea {
                                id: errorMouse
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }

                // Translate button
                Controls.AppButton {
                    visible: root.hasTrack && root.lyricsStatus === Guide.LyricsStatus.Found && !!AppContext.lyrics && AppContext.lyrics.translationAvailable
                    checkable: true
                    checked: !!AppContext.lyrics && AppContext.lyrics.showTranslation
                    text: (AppContext.lyrics && AppContext.lyrics.translating)
                        ? qsTr("Translating…")
                        : qsTr("Translate")
                    ToolTip.visible: hovered
                    ToolTip.text: {
                        if (!AppContext.lyrics) return ""
                        if (AppContext.lyrics.translationError.length > 0) {
                            return qsTr("Retry translation")
                        }
                        return AppContext.lyrics.showTranslation ? qsTr("Hide translation") : qsTr("Translate lyrics")
                    }
                    onClicked: {
                        if (AppContext.lyrics) {
                            if (AppContext.lyrics.showTranslation && AppContext.lyrics.translationError.length > 0) {
                                AppContext.lyrics.retryTranslation()
                            } else {
                                AppContext.lyrics.showTranslation = !AppContext.lyrics.showTranslation
                            }
                        }
                    }
                }

                // "..." more button
                Controls.AppButton {
                    text: "…"
                    implicitWidth: Theme.controlHeight
                    implicitHeight: Theme.controlHeight
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Lyrics Options")
                    enabled: root.hasTrack
                    onClicked: moreMenu.popup(this, 0, height)
                }
            }
        }
    }
}
