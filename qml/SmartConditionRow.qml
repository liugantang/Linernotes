// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    property smartCondition conditionData
    property int conditionIndex: 0

    signal removeClicked()
    signal modified()

    implicitHeight: Theme.controlHeight
    implicitWidth: layout.implicitWidth

    readonly property int fieldType: AppContext.playlists
        ? AppContext.playlists.smartFieldKind(conditionData ? conditionData.field : Library.SmartField.Title)
        : Library.SmartFieldKind.Text

    readonly property var fieldsModel: getFieldsModel()

    function getFieldsModel() {
        if (!AppContext.playlists) {
            return []
        }
        const fields = AppContext.playlists.smartFields()
        const res = []
        for (let i = 0; i < fields.length; ++i) {
            res.push({
                text: AppContext.playlists.fieldLabel(fields[i]),
                value: fields[i]
            })
        }
        return res
    }

    function getOpsModel(field) {
        if (!AppContext.playlists || field === undefined) {
            return []
        }
        const ops = AppContext.playlists.smartOps(field)
        const res = []
        for (let i = 0; i < ops.length; ++i) {
            res.push({
                text: AppContext.playlists.opLabel(ops[i]),
                value: ops[i]
            })
        }
        return res
    }

    function syncIndices() {
        if (!root.conditionData) {
            return
        }
        if (fieldCombo && fieldsModel && fieldsModel.length > 0) {
            fieldCombo.currentIndex = fieldsModel.findIndex(m => m.value === root.conditionData.field)
        }
        if (opCombo && opCombo.model && opCombo.model.length > 0) {
            opCombo.currentIndex = opCombo.model.findIndex(m => m.value === root.conditionData.op)
        }
    }

    onConditionDataChanged: {
        syncIndices()
    }

    Component.onCompleted: {
        syncIndices()
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: Theme.spacingSmall

        Controls.AppComboBox {
            id: fieldCombo
            Layout.preferredWidth: 130
            textRole: "text"
            valueRole: "value"
            model: root.fieldsModel

            onModelChanged: {
                root.syncIndices()
            }

            Component.onCompleted: {
                root.syncIndices()
            }

            onActivated: (index) => {
                const item = model[index]
                if (!item) {
                    return
                }
                const newField = item.value
                root.conditionData.field = newField
                const ops = AppContext.playlists ? AppContext.playlists.smartOps(newField) : []
                root.conditionData.op = ops.length > 0 ? ops[0] : Library.SmartOp.Contains
                const type = AppContext.playlists ? AppContext.playlists.smartFieldKind(newField) : Library.SmartFieldKind.Text

                if (type === Library.SmartFieldKind.Number) {
                    root.conditionData.value = 0
                    root.conditionData.value2 = 0
                } else if (type === Library.SmartFieldKind.Date) {
                    root.conditionData.value = 30
                    root.conditionData.value2 = undefined
                } else if (type === Library.SmartFieldKind.Bool) {
                    root.conditionData.value = undefined
                    root.conditionData.value2 = undefined
                } else {
                    root.conditionData.value = ""
                    root.conditionData.value2 = undefined
                }

                opCombo.model = root.getOpsModel(newField)
                root.syncIndices()
                root.modified()
            }
        }

        Controls.AppComboBox {
            id: opCombo
            Layout.preferredWidth: 140
            textRole: "text"
            valueRole: "value"
            model: root.getOpsModel(root.conditionData ? root.conditionData.field : Library.SmartField.Title)

            onModelChanged: {
                root.syncIndices()
            }

            Component.onCompleted: {
                root.syncIndices()
            }

            onActivated: (index) => {
                const item = model[index]
                if (!item) {
                    return
                }
                root.conditionData.op = item.value
                root.modified()
            }
        }

        // Value Input Area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Text input
            Controls.AppTextField {
                anchors.fill: parent
                visible: root.fieldType === Library.SmartFieldKind.Text
                placeholderText: qsTr("Value")
                text: (root.conditionData && root.conditionData.value !== undefined && root.conditionData.value !== null)
                    ? String(root.conditionData.value)
                    : ""
                onTextEdited: {
                    root.conditionData.value = text
                    root.modified()
                }
            }

            // Single number input (non-between)
            Controls.AppTextField {
                anchors.fill: parent
                visible: root.fieldType === Library.SmartFieldKind.Number && (root.conditionData && root.conditionData.op !== Library.SmartOp.Between)
                placeholderText: "0"
                text: (root.conditionData && root.conditionData.value !== undefined && root.conditionData.value !== null)
                    ? String(root.conditionData.value)
                    : "0"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: {
                    root.conditionData.value = Number(text) || 0
                    root.modified()
                }
            }

            // Between number inputs
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingTiny
                visible: root.fieldType === Library.SmartFieldKind.Number && (root.conditionData && root.conditionData.op === Library.SmartOp.Between)

                Controls.AppTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Min")
                    text: (root.conditionData && root.conditionData.value !== undefined && root.conditionData.value !== null)
                        ? String(root.conditionData.value)
                        : "0"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onTextEdited: {
                        root.conditionData.value = Number(text) || 0
                        root.modified()
                    }
                }

                Label {
                    text: qsTr("and")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }

                Controls.AppTextField {
                    Layout.fillWidth: true
                    placeholderText: qsTr("Max")
                    text: (root.conditionData && root.conditionData.value2 !== undefined && root.conditionData.value2 !== null)
                        ? String(root.conditionData.value2)
                        : "0"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onTextEdited: {
                        root.conditionData.value2 = Number(text) || 0
                        root.modified()
                    }
                }
            }

            // Date input (N days)
            RowLayout {
                anchors.fill: parent
                spacing: Theme.spacingTiny
                visible: root.fieldType === Library.SmartFieldKind.Date

                Controls.AppTextField {
                    Layout.fillWidth: true
                    placeholderText: "30"
                    text: (root.conditionData && root.conditionData.value !== undefined && root.conditionData.value !== null)
                        ? String(root.conditionData.value)
                        : "30"
                    inputMethodHints: Qt.ImhDigitsOnly
                    onTextEdited: {
                        root.conditionData.value = Number(text) || 0
                        root.modified()
                    }
                }

                Label {
                    text: qsTr("days")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        Controls.IconButton {
            icon.source: "icons/trash-2.svg"
            toolTip: qsTr("Remove Condition")
            onClicked: {
                root.removeClicked()
            }
        }
    }
}
