// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Rectangle {
    id: root

    required property var queuePanel
    required property int index
    required property string source
    required property var trackId
    required property var uid
    required property bool isCurrent
    required property string title
    required property string artist
    required property string durationText
    required property string coverHash

    readonly property bool isSelectedRow: root.queuePanel.selection.revision >= 0
        && root.queuePanel.selection.isSelected(root.index)
    readonly property bool isHovered: rowMouseArea.containsMouse || removeButton.hovered
        || dragHandleArea.containsMouse

    width: ListView.view ? ListView.view.width : 280
    height: Theme.trackRowHeight
    color: {
        if (isSelectedRow) {
            return Theme.itemSelected
        }
        if (isHovered) {
            return Theme.hoverOverlay
        }
        return "transparent"
    }

    MouseArea {
        id: rowMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                root.queuePanel.selection.select(root.index, mouse.modifiers)
                root.queuePanel.currentIndex = root.index
                root.queuePanel.forceActiveFocus()
            } else if (mouse.button === Qt.RightButton) {
                if (!root.queuePanel.selection.isSelected(root.index)) {
                    root.queuePanel.selection.select(root.index, Qt.NoModifier)
                    root.queuePanel.currentIndex = root.index
                }
                root.queuePanel.openContextMenu(root.index, root.trackId, rowMouseArea)
            }
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton && AppContext.queueModel) {
                AppContext.queueModel.playAt(root.index)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingTiny
        anchors.rightMargin: Theme.spacingSmall
        spacing: Theme.spacingSmall

        // Drag handle
        Item {
            z: 10
            Layout.preferredWidth: 16
            Layout.preferredHeight: 24
            Layout.alignment: Qt.AlignVCenter

            Image {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: "icons/grip-vertical.svg"
                opacity: dragHandleArea.containsMouse ? 0.9 : 0.35
            }

            MouseArea {
                id: dragHandleArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeVerCursor
                preventStealing: true

                onPressed: (mouse) => {
                    const mapped = dragHandleArea.mapToItem(root.queuePanel, mouse.x, mouse.y)
                    root.queuePanel.startDrag(root.index, mapped.y)
                }

                onPositionChanged: (mouse) => {
                    const mapped = dragHandleArea.mapToItem(root.queuePanel, mouse.x, mouse.y)
                    root.queuePanel.updateDrag(mapped.y)
                }

                onReleased: {
                    root.queuePanel.endDrag()
                }

                onCanceled: {
                    root.queuePanel.endDrag()
                }
            }
        }

        // Cover + Play Indicator
        Item {
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                id: coverRect
                anchors.fill: parent
                radius: Theme.radiusSmall
                color: Theme.surfaceVariant
                clip: true

                Image {
                    anchors.centerIn: parent
                    source: "icons/music.svg"
                    width: 18
                    height: 18
                    visible: !coverImage.visible
                    opacity: 0.5
                }

                Image {
                    id: coverImage
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: (root.coverHash && root.coverHash.length > 0)
                        ? ("image://cover/" + encodeURIComponent(root.coverHash)) : ""
                    sourceSize: Qt.size(72, 72)
                    visible: status === Image.Ready && root.coverHash.length > 0
                }

                // Playing indicator overlay
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(0, 0, 0, 0.45)
                    visible: root.isCurrent

                    Image {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        source: "icons/audio-lines.svg"
                    }
                }
            }
        }

        // Title and Artist
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Label {
                text: root.title && root.title.length > 0 ? root.title : qsTr("Unknown Title")
                font.pixelSize: Theme.fontSizeNormal
                font.bold: root.isCurrent
                color: root.isCurrent ? Theme.accent : Theme.text
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Label {
                text: root.artist && root.artist.length > 0 ? root.artist : ""
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
                visible: text.length > 0
            }
        }

        // Right side: Duration or Remove Button
        Item {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter

            Label {
                anchors.centerIn: parent
                text: root.durationText
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                visible: !(root.isHovered && !root.isCurrent)
            }

            Controls.IconButton {
                id: removeButton
                anchors.centerIn: parent
                implicitWidth: 28
                implicitHeight: 28
                icon.source: "icons/x.svg"
                toolTip: qsTr("Remove from Queue")
                visible: root.isHovered && !root.isCurrent
                onClicked: {
                    if (AppContext.queueModel) {
                        AppContext.queueModel.removeItems([root.index])
                    }
                }
            }
        }
    }
}
