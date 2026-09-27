// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    property string titleText: qsTr("New Playlist")
    property string acceptButtonText: qsTr("Save")
    property string initialText: ""

    signal accepted(string name)

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay

    implicitWidth: 360
    implicitHeight: layout.implicitHeight + topPadding + bottomPadding
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

    function accept() {
        const name = nameField.text.trim()
        if (name.length === 0) {
            return
        }
        root.close()
        root.accepted(name)
    }

    function openWithText(text) {
        initialText = text || ""
        open()
    }

    onOpened: {
        nameField.text = initialText
        nameField.selectAll()
        nameField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        id: layout
        spacing: Theme.spacingMedium

        Label {
            text: root.titleText
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }

        Controls.AppTextField {
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Playlist Name")
            Keys.onReturnPressed: root.accept()
            Keys.onEnterPressed: root.accept()
            Keys.onEscapePressed: root.close()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacingSmall

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Cancel")
                onClicked: root.close()
            }

            Controls.AppButton {
                primary: true
                text: root.acceptButtonText
                enabled: nameField.text.trim().length > 0
                onClicked: root.accept()
            }
        }
    }
}
