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
    anchors.centerIn: Overlay.overlay

    implicitWidth: 540
    implicitHeight: Math.min(640, flickable.contentHeight + topPadding + bottomPadding)
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

    function openForTracks(ids) {
        if (!ids || ids.length === 0) {
            return
        }
        if (AppContext.tagEditor && AppContext.tagEditor.load(ids)) {
            open()
        }
    }

    contentItem: Flickable {
        id: flickable
        clip: true
        contentHeight: layout.implicitHeight
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: layout
            width: flickable.width
            spacing: Theme.spacingMedium

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Edit Tags")
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: Theme.text
                }

                Label {
                    text: (AppContext.tagEditor && AppContext.tagEditor.trackCount > 1)
                        ? qsTr("(%1 tracks)").arg(AppContext.tagEditor.trackCount)
                        : ""
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    visible: text.length > 0
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            GridLayout {
                id: grid
                columns: 6
                Layout.fillWidth: true
                rowSpacing: Theme.spacingSmall
                columnSpacing: Theme.spacingSmall

                Repeater {
                    model: AppContext.tagEditor
                    delegate: ColumnLayout {
                        id: fieldDelegate
                        required property var model
                        required property int index

                        Layout.columnSpan: {
                            if (index < 6) return 6
                            if (index === 6) return 2
                            return 1
                        }
                        Layout.fillWidth: true
                        spacing: Theme.spacingTiny

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingTiny

                            Label {
                                text: model.label
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: Theme.accent
                                visible: model.edited
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingTiny

                            Controls.AppTextField {
                                id: textField
                                Layout.fillWidth: true
                                text: model.value
                                placeholderText: model.mixed ? qsTr("Multiple values") : ""
                                enabled: model.editable
                                onTextEdited: {
                                    AppContext.tagEditor.setValue(fieldDelegate.index, text)
                                }
                            }

                            Controls.IconButton {
                                icon.source: "icons/rotate-ccw.svg"
                                toolTip: qsTr("Revert to file tag")
                                visible: model.overridden
                                onClicked: AppContext.tagEditor.revert(fieldDelegate.index)
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

            Label {
                Layout.fillWidth: true
                text: qsTr("Changes are saved to the library only; audio files are not modified.")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                visible: AppContext.tagEditor && AppContext.tagEditor.errorText.length > 0
                text: AppContext.tagEditor ? AppContext.tagEditor.errorText : ""
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.errorText
                wrapMode: Text.Wrap
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
                        if (AppContext.tagEditor && AppContext.tagEditor.save()) {
                            root.close()
                        }
                    }
                }
            }
        }
    }
}
