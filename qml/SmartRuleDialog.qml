// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Popup {
    id: root

    property int targetPlaylistId: 0
    property string initialName: ""
    property smartRule rule

    property string errorMessage: ""

    signal playlistCreated(int id)

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    implicitWidth: 600
    implicitHeight: Math.min(620, layout.implicitHeight + topPadding + bottomPadding)
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

    readonly property var matchModesModel: [
        //: Smart playlist rule match mode: match all conditions
        { text: qsTr("All"), value: Library.SmartMatch.All },
        //: Smart playlist rule match mode: match any condition
        { text: qsTr("Any"), value: Library.SmartMatch.Any }
    ]

    function syncCombos() {
        if (matchCombo && matchModesModel && matchModesModel.length > 0) {
            matchCombo.currentIndex = matchModesModel.findIndex(m => m.value === root.rule.match)
        }
        if (sortRow) {
            sortRow.syncCombos()
        }
    }

    function defaultCondition() {
        return {
            field: Library.SmartField.Title,
            op: Library.SmartOp.Contains
        };
    }

    function openNew() {
        targetPlaylistId = 0
        initialName = ""
        rule = {
            match: Library.SmartMatch.All,
            conditions: [ defaultCondition() ],
            sortKey: Library.TrackSortKey.Default,
            sortOrder: Qt.AscendingOrder,
            limit: 0,
            playedFrom: "",
            playedTo: ""
        }
        errorMessage = ""
        nameField.text = ""
        playedFromField.text = ""
        playedToField.text = ""
        open()
        syncCombos()
    }

    function openEdit(id, name) {
        targetPlaylistId = id
        initialName = name || ""
        let r = AppContext.playlists ? AppContext.playlists.rule(id) : null
        if (!r || r.conditions === undefined) {
            r = {
                match: Library.SmartMatch.All,
                conditions: [],
                sortKey: Library.TrackSortKey.Default,
                sortOrder: Qt.AscendingOrder,
                limit: 0,
                playedFrom: "",
                playedTo: ""
            }
        }
        rule = r
        errorMessage = ""
        nameField.text = initialName
        playedFromField.text = r.playedFrom || ""
        playedToField.text = r.playedTo || ""
        open()
        syncCombos()
    }

    function addCondition() {
        let r = rule;
        r.conditions = r.conditions.concat([defaultCondition()]);
        rule = r;
    }

    function removeCondition(index) {
        let r = rule;
        const list = r.conditions.slice();
        list.splice(index, 1);
        r.conditions = list;
        rule = r;
    }

    function updateCondition(index, cond) {
        let r = rule;
        let list = r.conditions.slice();
        list[index] = cond;
        r.conditions = list;
        rule = r;
    }

    function accept() {
        const name = nameField.text.trim()
        if (name.length === 0) {
            errorMessage = qsTr("Please enter a playlist name")
            return
        }

        if (targetPlaylistId === 0) {
            const newId = AppContext.playlists.createSmart(name, rule)
            if (newId > 0) {
                root.close()
                root.playlistCreated(newId)
            } else {
                errorMessage = qsTr("Failed to create smart playlist. Please check your rules.")
            }
        } else {
            if (name !== initialName) {
                AppContext.playlists.rename(targetPlaylistId, name)
            }
            if (AppContext.playlists.setRule(targetPlaylistId, rule)) {
                root.close()
            } else {
                errorMessage = qsTr("Failed to update smart playlist rules.")
            }
        }
    }

    onOpened: {
        syncCombos()
        if (targetPlaylistId === 0) {
            nameField.selectAll()
            nameField.forceActiveFocus()
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

            Label {
                text: root.targetPlaylistId === 0
                    ? qsTr("New Smart Playlist")
                    : qsTr("Edit Smart Playlist Rules")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            // Playlist Name
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Name:")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.text
                    Layout.preferredWidth: 60
                }

                Controls.AppTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Playlist Name")
                    Keys.onReturnPressed: root.accept()
                    Keys.onEnterPressed: root.accept()
                    Keys.onEscapePressed: root.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            // Match all / any
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Match")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.text
                }

                Controls.AppComboBox {
                    id: matchCombo
                    Layout.preferredWidth: 100
                    textRole: "text"
                    valueRole: "value"
                    model: root.matchModesModel

                    onModelChanged: {
                        root.syncCombos()
                    }

                    Component.onCompleted: {
                        root.syncCombos()
                    }

                    onActivated: (index) => {
                        if (model[index]) {
                            let r = root.rule;
                            r.match = model[index].value;
                            root.rule = r;
                        }
                    }
                }

                Label {
                    text: qsTr("of the following conditions:")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.text
                    Layout.fillWidth: true
                }
            }

            // Conditions List
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Repeater {
                    // 按条数而不是列表本身建行：编辑写回 rule 时不重建行，输入框不丢焦点
                    model: root.rule.conditions.length
                    delegate: SmartConditionRow {
                        required property int index

                        Layout.fillWidth: true
                        conditionData: root.rule.conditions[index]
                        conditionIndex: index
                        onRemoveClicked: root.removeCondition(index)
                        onModified: root.updateCondition(index, conditionData)
                    }
                }

                Controls.AppButton {
                    text: qsTr("Add Condition")
                    icon.source: "icons/list-plus.svg"
                    onClicked: {
                        root.addCondition()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            // Play count period
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Play count period:")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.text
                }

                Controls.AppTextField {
                    id: playedFromField
                    Layout.preferredWidth: 110
                    placeholderText: "yyyy-MM-dd"
                    text: root.rule ? (root.rule.playedFrom || "") : ""
                    onTextEdited: {
                        let r = root.rule
                        r.playedFrom = text.trim()
                        root.rule = r
                    }
                }

                Label {
                    text: qsTr("to")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                }

                Controls.AppTextField {
                    id: playedToField
                    Layout.preferredWidth: 110
                    placeholderText: "yyyy-MM-dd"
                    text: root.rule ? (root.rule.playedTo || "") : ""
                    onTextEdited: {
                        let r = root.rule
                        r.playedTo = text.trim()
                        root.rule = r
                    }
                }

                Label {
                    text: qsTr("Only affects play/skip/completed counts")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.divider
            }

            // Sort and Limit
            SmartRuleSortRow {
                id: sortRow
                Layout.fillWidth: true
                sortKey: root.rule.sortKey
                sortOrder: root.rule.sortOrder
                limitEnabled: root.rule.limit > 0
                limitCount: root.rule.limit > 0 ? root.rule.limit : 50
                onSortKeyChanged: {
                    let r = root.rule;
                    r.sortKey = sortKey;
                    root.rule = r;
                }
                onSortOrderChanged: {
                    let r = root.rule;
                    r.sortOrder = sortOrder;
                    root.rule = r;
                }
                onLimitEnabledChanged: {
                    let r = root.rule;
                    if (limitEnabled) {
                        r.limit = limitCount > 0 ? limitCount : 50;
                    } else {
                        r.limit = 0;
                    }
                    root.rule = r;
                }
                onLimitCountChanged: {
                    if (limitEnabled) {
                        let r = root.rule;
                        r.limit = limitCount;
                        root.rule = r;
                    }
                }
            }

            // Error banner
            Label {
                Layout.fillWidth: true
                visible: root.errorMessage.length > 0
                text: root.errorMessage
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.errorText
                wrapMode: Text.Wrap
            }

            // Actions
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Item {
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: {
                        root.close()
                    }
                }

                Controls.AppButton {
                    primary: true
                    text: root.targetPlaylistId === 0 ? qsTr("Create") : qsTr("Save")
                    enabled: nameField.text.trim().length > 0
                    onClicked: {
                        root.accept()
                    }
                }
            }
        }
    }
}
