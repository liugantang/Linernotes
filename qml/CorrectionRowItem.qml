// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Rectangle {
    id: root

    property bool isEditing: false
    readonly property bool isCompact: width < 560
    readonly property bool isSelected: AppContext.review && AppContext.review.listModel
        && AppContext.review.listModel.selection && AppContext.review.listModel.selection.revision >= 0
        && AppContext.review.listModel.selection.isSelected(index)
    implicitHeight: Math.max(56, contentRow.implicitHeight + Theme.spacingSmall * 2)
    color: isSelected ? Theme.itemSelected : (mouseArea.containsMouse ? Theme.itemHover : "transparent")

    function sourceText(src) {
        if (src === Library.CorrectionSource.Rule) return qsTr("Rule")
        if (src === Library.CorrectionSource.Llm) return qsTr("LLM")
        if (src === Library.CorrectionSource.MusicBrainz) return "MusicBrainz"
        return src === Library.CorrectionSource.User ? qsTr("User") : ""
    }
    function statusText(st) {
        if (model.stale) return qsTr("Stale")
        if (st === Library.CorrectionStatus.Pending) return qsTr("Pending")
        if (st === Library.CorrectionStatus.Accepted) return qsTr("Accepted")
        if (st === Library.CorrectionStatus.Rejected) return qsTr("Rejected")
        return st === Library.CorrectionStatus.Reverted ? qsTr("Reverted") : ""
    }
    function statusColor(st) {
        if (model.stale) return Theme.errorText
        if (st === Library.CorrectionStatus.Pending) return Theme.isDark ? "#f9e2af" : "#b45309"
        if (st === Library.CorrectionStatus.Accepted) return Theme.isDark ? "#a6e3a1" : "#16a34a"
        return st === Library.CorrectionStatus.Rejected ? Theme.errorText : Theme.textSecondary
    }
    function statusBgColor(st) {
        if (model.stale) return Theme.errorBackground
        if (st === Library.CorrectionStatus.Pending) return Theme.isDark ? "#3e3220" : "#fef3c7"
        if (st === Library.CorrectionStatus.Accepted) return Theme.isDark ? "#1e3a29" : "#dcfce7"
        return st === Library.CorrectionStatus.Rejected ? Theme.errorBackground : Theme.surfaceVariant
    }
    function saveEdit() {
        if (AppContext.review) {
            AppContext.review.acceptEdited(model.correctionId, editField.text)
        }
        root.isEditing = false
    }
    function selectRow(modifiers) {
        if (AppContext.review && AppContext.review.listModel && AppContext.review.listModel.selection) {
            AppContext.review.listModel.selection.select(index, modifiers)
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onClicked: (mouse) => root.selectRow(mouse.modifiers)
        onDoubleClicked: {
            if (model.status === Library.CorrectionStatus.Pending && !model.stale) {
                root.isEditing = true
                editField.forceActiveFocus()
                editField.selectAll()
            }
        }
    }

    RowLayout {
        id: contentRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingSmall
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // CheckBox
        Controls.AppCheckBox {
            checked: root.isSelected
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.selectRow(Qt.ControlModifier)
        }

        // Main content: Subject + Details + Field + Change + Reason
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignVCenter
            spacing: 3

            // Subject + Detail row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: model.subject
                    font.pixelSize: Theme.fontSizeNormal
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.maximumWidth: 280
                }
                Label {
                    text: model.detail
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    visible: model.detail.length > 0
                }
            }

            // Field + Change row
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                // Field tag
                Rectangle {
                    color: Theme.surfaceVariant
                    radius: Theme.radiusSmall
                    Layout.preferredHeight: 20
                    Layout.preferredWidth: fieldLabel.implicitWidth + Theme.spacingSmall * 2
                    Label {
                        id: fieldLabel
                        anchors.centerIn: parent
                        text: model.field
                        font.pixelSize: Theme.fontSizeSmall - 1
                        color: Theme.textSecondary
                    }
                }

                // Proposed change
                RowLayout {
                    id: changeRow
                    spacing: Theme.spacingSmall
                    visible: !root.isEditing
                    Layout.fillWidth: true

                    Label {
                        id: oldValueLabel
                        text: model.oldValue
                        font.strikeout: true
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        visible: model.oldValue.length > 0
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 30
                        Layout.maximumWidth: implicitWidth
                    }
                    Label {
                        id: arrowLabel
                        text: "→"
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        visible: model.oldValue.length > 0
                    }
                    Label {
                        id: newValueLabel
                        text: model.newValue
                        font.bold: true
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 30
                        Layout.maximumWidth: implicitWidth
                    }
                    Item { Layout.fillWidth: true }
                }

                // Inline editor
                RowLayout {
                    visible: root.isEditing
                    spacing: Theme.spacingSmall
                    Layout.fillWidth: true
                    Controls.AppTextField {
                        id: editField
                        text: model.newValue
                        Layout.fillWidth: true
                        Layout.maximumWidth: 300
                        Layout.preferredHeight: 28
                        onAccepted: root.saveEdit()
                        Keys.onEscapePressed: root.isEditing = false
                    }
                    Controls.AppButton {
                        text: qsTr("Save")
                        primary: true
                        Layout.preferredHeight: 28
                        onClicked: root.saveEdit()
                    }
                    Controls.AppButton {
                        text: qsTr("Cancel")
                        Layout.preferredHeight: 28
                        onClicked: root.isEditing = false
                    }
                }
            }

            // Reason row
            Label {
                text: model.reason
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Layout.fillWidth: true
                visible: model.reason.length > 0
            }
        }

        // Right side: Confidence + Source + Status
        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: root.isCompact ? Theme.spacingSmall : Theme.spacingMedium

            // Confidence
            ColumnLayout {
                Layout.preferredWidth: root.isCompact ? 44 : 64
                spacing: 2
                Layout.alignment: Qt.AlignVCenter
                Label {
                    text: Math.round(model.confidence * 100) + "%"
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: Theme.text
                    Layout.alignment: Qt.AlignHCenter
                }
                Rectangle {
                    Layout.preferredWidth: root.isCompact ? 36 : 50
                    Layout.preferredHeight: 4
                    Layout.alignment: Qt.AlignHCenter
                    radius: 2
                    color: Theme.surfaceVariant
                    Rectangle {
                        width: parent.width * Math.max(0.0, Math.min(1.0, model.confidence))
                        height: parent.height
                        radius: 2
                        color: Theme.accent
                    }
                }
            }

            // Source badge
            Rectangle {
                visible: !root.isCompact
                color: Theme.surfaceVariant
                radius: Theme.radiusSmall
                Layout.preferredHeight: 22
                Layout.preferredWidth: 60
                Layout.alignment: Qt.AlignVCenter
                Label {
                    anchors.centerIn: parent
                    text: root.sourceText(model.source)
                    font.pixelSize: Theme.fontSizeSmall - 1
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
            }

            // Status badge
            Rectangle {
                color: root.statusBgColor(model.status)
                radius: Theme.radiusSmall
                border.color: model.stale ? Theme.errorBorder : root.statusColor(model.status)
                border.width: 1
                Layout.preferredHeight: 24
                Layout.preferredWidth: root.isCompact ? 68 : 76
                Layout.alignment: Qt.AlignVCenter
                Label {
                    anchors.centerIn: parent
                    text: root.statusText(model.status)
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: true
                    color: root.statusColor(model.status)
                    elide: Text.ElideRight
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.divider
    }
}
