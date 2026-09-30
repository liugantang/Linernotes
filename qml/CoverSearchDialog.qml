// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    property var albumId: 0
    property int selectedIndex: -1

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay

    implicitWidth: 680
    implicitHeight: 560
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

    function openDialog(id) {
        albumId = id
        selectedIndex = -1
        open()
        if (AppContext.coverSearch) {
            AppContext.coverSearch.search(id)
        }
    }

    onClosed: {
        if (AppContext.coverSearch && AppContext.coverSearch.busy) {
            AppContext.coverSearch.cancel()
        }
    }

    Connections {
        target: (typeof AppContext !== "undefined" && AppContext) ? AppContext.coverSearch : null
        function onCoverChanged(id) {
            if (root.visible && root.albumId === id) {
                root.close()
            }
        }
    }

    function formatSubtitle(item) {
        if (!item) return ""
        var parts = []
        if (item.artist) {
            parts.push(item.artist)
        }
        if (item.year !== undefined && item.year !== null && item.year > 0) {
            parts.push(item.year)
        }
        if (item.trackCount !== undefined && item.trackCount > 0) {
            parts.push(qsTr("%n track(s)", "", item.trackCount))
        }
        if (item.country) {
            parts.push(item.country.toUpperCase())
        }
        return parts.join(" · ")
    }

    contentItem: ColumnLayout {
        id: layout
        spacing: Theme.spacingMedium

        // Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Find Cover Online")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                visible: !!(AppContext.coverSearch && AppContext.coverSearch.busy)
                text: qsTr("Searching iTunes…")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Center / Candidates Grid
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Empty / Busy placeholder
            Label {
                anchors.centerIn: parent
                visible: (!AppContext.coverSearch || AppContext.coverSearch.results.length === 0)
                text: {
                    if (AppContext.coverSearch && AppContext.coverSearch.errorText.length > 0) {
                        return AppContext.coverSearch.errorText
                    }
                    if (AppContext.coverSearch && AppContext.coverSearch.busy) {
                        return qsTr("Searching iTunes…")
                    }
                    return qsTr("No covers found")
                }
                font.pixelSize: Theme.fontSizeLarge
                color: (AppContext.coverSearch && AppContext.coverSearch.errorText.length > 0) ? Theme.errorText : Theme.textSecondary
            }

            GridView {
                id: gridView
                anchors.fill: parent
                clip: true
                focus: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                visible: !!(AppContext.coverSearch && AppContext.coverSearch.results.length > 0)
                model: (AppContext.coverSearch) ? AppContext.coverSearch.results : []

                readonly property int availableWidth: Math.max(100, width - ScrollBar.vertical.width) // 始终预留滚动条宽度，避免与 visible 形成绑定循环
                readonly property int columns: Math.max(1, Math.floor((availableWidth + Theme.spacingMedium) / (140 + Theme.spacingMedium)))
                cellWidth: Math.floor(availableWidth / columns)
                readonly property int cardWidth: cellWidth - Theme.spacingSmall
                cellHeight: cardWidth + 48 + Theme.spacingSmall

                ScrollBar.vertical: Controls.AppScrollBar {}

                delegate: Item {
                    id: candidateCard
                    required property int index
                    required property var modelData

                    width: gridView.cellWidth
                    height: gridView.cellHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingTiny
                        radius: Theme.cardBorderRadius
                        color: {
                            if (root.selectedIndex === candidateCard.index) {
                                return Theme.itemSelected
                            }
                            if (cardMouse.containsMouse) {
                                return Theme.itemHover
                            }
                            return "transparent"
                        }
                        border.color: root.selectedIndex === candidateCard.index ? Theme.accent : "transparent"
                        border.width: 2

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingTiny
                            spacing: Theme.spacingTiny

                            // Cover Image Container
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: width

                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.coverBorderRadius
                                    color: Theme.surfaceVariant
                                    clip: true

                                    Image {
                                        id: thumbImg
                                        anchors.fill: parent
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectCrop
                                        source: (candidateCard.modelData && candidateCard.modelData.thumbnailUrl) ? candidateCard.modelData.thumbnailUrl : ""
                                        sourceSize: Qt.size(200, 200)
                                        visible: status === Image.Ready
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: thumbImg.status !== Image.Ready
                                        text: "…"
                                        font.pixelSize: Theme.fontSizeLarge
                                        color: Theme.textSecondary
                                    }
                                }
                            }

                            // Line 1: Title
                            Label {
                                text: (candidateCard.modelData && candidateCard.modelData.title) ? candidateCard.modelData.title : ""
                                font.pixelSize: Theme.fontSizeSmall
                                font.bold: true
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                maximumLineCount: 1
                            }

                            // Line 2: Artist · Year · N tracks · Country
                            Label {
                                text: root.formatSubtitle(candidateCard.modelData)
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                maximumLineCount: 1
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.selectedIndex = candidateCard.index
                            }
                            onDoubleClicked: {
                                root.selectedIndex = candidateCard.index
                                if (AppContext.coverSearch) {
                                    AppContext.coverSearch.choose(candidateCard.index)
                                }
                            }
                        }
                    }
                }
            }
        }

        // Error message banner if errorText while having results
        Label {
            visible: !!(AppContext.coverSearch && AppContext.coverSearch.errorText.length > 0 && AppContext.coverSearch.results.length > 0)
            text: AppContext.coverSearch ? AppContext.coverSearch.errorText : ""
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.errorText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        // Action Buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Cancel")
                onClicked: {
                    if (AppContext.coverSearch) {
                        AppContext.coverSearch.cancel()
                    }
                    root.close()
                }
            }

            Controls.AppButton {
                primary: true
                text: (AppContext.coverSearch && AppContext.coverSearch.busy && root.selectedIndex >= 0) ? qsTr("Downloading…") : qsTr("Set as Cover")
                enabled: root.selectedIndex >= 0 && (!AppContext.coverSearch || !AppContext.coverSearch.busy)
                onClicked: {
                    if (AppContext.coverSearch && root.selectedIndex >= 0) {
                        AppContext.coverSearch.choose(root.selectedIndex)
                    }
                }
            }
        }
    }
}
