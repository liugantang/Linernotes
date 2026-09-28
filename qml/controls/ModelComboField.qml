// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

TextField {
    id: control

    property var models: []
    property bool busy: false

    signal picked(string model)

    implicitHeight: Theme.controlHeight
    implicitWidth: 200

    font.pixelSize: Theme.fontSizeNormal
    color: control.enabled ? Theme.text : Theme.textSecondary
    selectedTextColor: Theme.accentText
    selectionColor: Theme.accent
    placeholderTextColor: Theme.textSecondary
    verticalAlignment: TextInput.AlignVCenter

    leftPadding: Theme.spacingSmall
    rightPadding: Theme.controlHeight

    background: Rectangle {
        color: control.enabled ? Theme.surfaceVariant : Theme.divider
        radius: Theme.radiusMedium
        border.color: (control.enabled && control.activeFocus) ? Theme.accent : "transparent"
        border.width: 1
    }

    readonly property var filteredModels: {
        if (!models || models.length === 0) {
            return []
        }
        const currentText = (text || "").trim().toLowerCase()
        if (currentText === "") {
            return models
        }
        for (let i = 0; i < models.length; ++i) {
            if (String(models[i]).toLowerCase() === currentText) {
                return models
            }
        }
        const result = []
        for (let i = 0; i < models.length; ++i) {
            if (String(models[i]).toLowerCase().indexOf(currentText) !== -1) {
                result.push(models[i])
            }
        }
        return result
    }

    function selectModel(modelName) {
        control.text = modelName
        popup.close()
        control.picked(modelName)
        control.editingFinished()
    }

    onTextEdited: {
        if (models && models.length > 0 && filteredModels.length > 0) {
            if (!popup.visible) {
                popup.open()
            }
        } else {
            popup.close()
        }
    }

    Keys.onPressed: function(event) {
        if (popup.visible) {
            if (event.key === Qt.Key_Down) {
                if (listView.currentIndex < listView.count - 1) {
                    listView.currentIndex++
                } else if (listView.currentIndex < 0 && listView.count > 0) {
                    listView.currentIndex = 0
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Up) {
                if (listView.currentIndex > 0) {
                    listView.currentIndex--
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (listView.currentIndex >= 0 && listView.currentIndex < filteredModels.length) {
                    control.selectModel(filteredModels[listView.currentIndex])
                    event.accepted = true
                }
            } else if (event.key === Qt.Key_Escape) {
                popup.close()
                event.accepted = true
            }
        } else {
            if (event.key === Qt.Key_Down && models && models.length > 0) {
                popup.open()
                if (listView.count > 0 && listView.currentIndex < 0) {
                    listView.currentIndex = 0
                }
                event.accepted = true
            }
        }
    }

    Item {
        id: rightActionItem
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingTiny
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.controlHeight - Theme.spacingTiny * 2
        height: Theme.controlHeight - Theme.spacingTiny * 2

        BusyIndicator {
            id: busyIndicator
            anchors.centerIn: parent
            width: 16
            height: 16
            running: control.busy
            visible: control.busy
        }

        Canvas {
            id: arrowCanvas
            anchors.centerIn: parent
            width: 10
            height: 6
            contextType: "2d"
            visible: !control.busy
            opacity: (control.enabled && control.models && control.models.length > 0) ? 1.0 : 0.4

            Connections {
                target: Theme
                function onIsDarkChanged() { arrowCanvas.requestPaint(); }
            }
            Connections {
                target: control
                function onModelsChanged() { arrowCanvas.requestPaint(); }
                function onEnabledChanged() { arrowCanvas.requestPaint(); }
            }

            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.moveTo(0, 0);
                ctx.lineTo(width, 0);
                ctx.lineTo(width / 2, height);
                ctx.closePath();
                ctx.fillStyle = Theme.text;
                ctx.fill();
            }
        }

        MouseArea {
            id: arrowMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: (control.enabled && !control.busy && control.models && control.models.length > 0)
                ? Qt.PointingHandCursor : Qt.ArrowCursor
            enabled: control.enabled && !control.busy && control.models && control.models.length > 0

            onClicked: {
                control.forceActiveFocus()
                if (popup.visible) {
                    popup.close()
                } else {
                    popup.open()
                }
            }
        }
    }

    Popup {
        id: popup
        y: control.height + Theme.spacingTiny
        width: control.width
        implicitHeight: Math.min(
            listView.contentHeight + topPadding + bottomPadding,
            8 * Theme.controlHeight + topPadding + bottomPadding
        )
        padding: Theme.spacingTiny
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radiusMedium
            border.color: Theme.divider
            border.width: 1
        }

        contentItem: ListView {
            id: listView
            clip: true
            implicitHeight: contentHeight
            model: popup.visible ? control.filteredModels : null
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: AppScrollBar { }

            delegate: ItemDelegate {
                id: itemDelegate
                width: listView.width
                height: Theme.controlHeight
                highlighted: listView.currentIndex === index

                contentItem: Text {
                    text: modelData
                    color: control.text === modelData ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontSizeNormal
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacingSmall
                    rightPadding: Theme.spacingSmall
                }

                background: Rectangle {
                    color: (itemDelegate.hovered || itemDelegate.highlighted) ? Theme.hoverOverlay : "transparent"
                    radius: Theme.radiusSmall
                }

                onClicked: {
                    control.selectModel(modelData)
                }
            }
        }
    }
}
