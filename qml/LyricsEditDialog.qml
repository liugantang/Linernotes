// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    property string initialText: ""

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay

    implicitWidth: 480
    implicitHeight: 440
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

    function openWithText(text) {
        initialText = text || ""
        open()
    }

    onOpened: {
        textArea.text = initialText
        textArea.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        id: layout
        spacing: Theme.spacingSmall

        Label {
            text: qsTr("Edit Lyrics")
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }

        Label {
            text: qsTr("Tip: Saving empty text will remove manual lyrics and restore automatic source.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ScrollView {
            id: scrollView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: Controls.AppScrollBar {}

            TextArea {
                id: textArea
                selectByMouse: true
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.text
                selectedTextColor: Theme.accentText
                selectionColor: Theme.accent
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("Enter LRC or plain text lyrics…")
                placeholderTextColor: Theme.textSecondary
                background: Rectangle {
                    color: Theme.surfaceVariant
                    border.color: textArea.activeFocus ? Theme.accent : Theme.divider
                    border.width: 1
                    radius: Theme.radiusMedium
                }
            }
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
                text: qsTr("Save")
                onClicked: {
                    if (AppContext.lyrics) {
                        AppContext.lyrics.saveManual(textArea.text)
                    }
                    root.close()
                }
            }
        }
    }
}
