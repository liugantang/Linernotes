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

    x: Math.round(((Overlay.overlay ? Overlay.overlay.width : 800) - width) / 2)
    y: Math.max(20, Math.round((Overlay.overlay ? Overlay.overlay.height : 600) * 0.08))
    width: Math.min(760, Overlay.overlay ? Overlay.overlay.width - 40 : 760)
    implicitHeight: Math.min(
        Overlay.overlay ? Overlay.overlay.height * 0.8 : 600,
        mainLayout.implicitHeight + topPadding + bottomPadding
    )

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

    function openPalette() {
        open()
        inputField.selectAll()
        inputField.forceActiveFocus()
    }

    onOpened: {
        inputField.forceActiveFocus()
    }

    function openChipEditor(chipData) {
        if (!chipData) return
        const kind = chipData.kind
        if (kind === NlqController.Match || kind === 1) {
            matchEditPopup.openDialog()
        } else if (kind === NlqController.Condition || kind === 2) {
            conditionEditPopup.openForCondition(chipData.index)
        } else if (kind === NlqController.PlayWindow || kind === 3) {
            playWindowEditPopup.openDialog()
        } else if (kind === NlqController.Sort || kind === 4) {
            sortEditPopup.openDialog()
        } else if (kind === NlqController.Limit || kind === 5) {
            limitEditPopup.openDialog()
        } else if (kind === NlqController.Entity || kind === 0) {
            entityEditPopup.openDialog()
        }
    }

    PlaylistNameDialog {
        id: playlistNameDialog
        titleText: qsTr("Save as Playlist")
        onAccepted: (name) => {
            if (AppContext.nlq) {
                AppContext.nlq.saveAsPlaylist(name)
            }
        }
    }

    // --- Chip Editor Popups ---

    Popup {
        id: conditionEditPopup
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 40 : 640)
        padding: Theme.spacingMedium
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        property int targetIndex: 0
        property var tempCondition: null

        function openForCondition(idx) {
            targetIndex = idx
            if (AppContext.nlq) {
                tempCondition = AppContext.nlq.conditionAt(idx)
            }
            open()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Edit Condition")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            SmartConditionRow {
                id: editConditionRow
                Layout.fillWidth: true
                conditionData: conditionEditPopup.tempCondition
                onRemoveClicked: {
                    conditionEditPopup.close()
                    if (AppContext.nlq) {
                        AppContext.nlq.removeChip(conditionEditPopup.targetIndex)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Item { Layout.fillWidth: true }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: conditionEditPopup.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Apply")
                    onClicked: {
                        conditionEditPopup.close()
                        if (conditionEditPopup.tempCondition && AppContext.nlq) {
                            AppContext.nlq.setCondition(conditionEditPopup.targetIndex, conditionEditPopup.tempCondition)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: playWindowEditPopup
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(420, Overlay.overlay ? Overlay.overlay.width - 40 : 420)
        padding: Theme.spacingMedium
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        function openDialog() {
            open()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Edit Play Period")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Layout.fillWidth: true

                Controls.AppTextField {
                    id: fromDateField
                    Layout.fillWidth: true
                    placeholderText: qsTr("From (yyyy-MM-dd)")
                }

                Label {
                    text: qsTr("to")
                    color: Theme.textSecondary
                }

                Controls.AppTextField {
                    id: toDateField
                    Layout.fillWidth: true
                    placeholderText: qsTr("To (yyyy-MM-dd)")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Controls.AppButton {
                    text: qsTr("Clear")
                    onClicked: {
                        playWindowEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setPlayWindow("", "")
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: playWindowEditPopup.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Apply")
                    onClicked: {
                        playWindowEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setPlayWindow(fromDateField.text.trim(), toDateField.text.trim())
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: sortEditPopup
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(380, Overlay.overlay ? Overlay.overlay.width - 40 : 380)
        padding: Theme.spacingMedium
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        function openDialog() {
            open()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Edit Sort")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            Controls.AppComboBox {
                id: sortKeyCombo
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("Default"), value: 0 },
                    { text: qsTr("Play count"), value: 1 },
                    { text: qsTr("Last played"), value: 2 },
                    { text: qsTr("Rating"), value: 3 },
                    { text: qsTr("Year"), value: 4 },
                    { text: qsTr("Date added"), value: 5 },
                    { text: qsTr("Duration"), value: 6 },
                    { text: qsTr("Random"), value: 7 }
                ]
            }

            Controls.AppComboBox {
                id: sortOrderCombo
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("Descending (↓)"), value: Qt.DescendingOrder },
                    { text: qsTr("Ascending (↑)"), value: Qt.AscendingOrder }
                ]
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Item { Layout.fillWidth: true }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: sortEditPopup.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Apply")
                    onClicked: {
                        sortEditPopup.close()
                        if (AppContext.nlq) {
                            const key = sortKeyCombo.currentValue !== undefined ? sortKeyCombo.currentValue : 0
                            const order = sortOrderCombo.currentValue !== undefined ? sortOrderCombo.currentValue : Qt.DescendingOrder
                            AppContext.nlq.setSort(key, order)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: limitEditPopup
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(320, Overlay.overlay ? Overlay.overlay.width - 40 : 320)
        padding: Theme.spacingMedium
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        function openDialog() {
            open()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Edit Limit")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            Controls.AppTextField {
                id: limitField
                Layout.fillWidth: true
                placeholderText: "50"
                inputMethodHints: Qt.ImhDigitsOnly
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingSmall

                Item { Layout.fillWidth: true }

                Controls.AppButton {
                    text: qsTr("Cancel")
                    onClicked: limitEditPopup.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Apply")
                    onClicked: {
                        limitEditPopup.close()
                        const val = parseInt(limitField.text, 10)
                        if (!isNaN(val) && val > 0 && AppContext.nlq) {
                            AppContext.nlq.setLimit(val)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: matchEditPopup
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, Overlay.overlay ? Overlay.overlay.width - 40 : 360)
        padding: Theme.spacingMedium
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        function openDialog() {
            open()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Match Conditions")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Layout.fillWidth: true

                Controls.AppButton {
                    Layout.fillWidth: true
                    text: qsTr("All conditions")
                    primary: AppContext.nlq && (AppContext.nlq.match === Library.SmartMatch.All || AppContext.nlq.match === 0)
                    onClicked: {
                        matchEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setMatch(Library.SmartMatch.All)
                        }
                    }
                }

                Controls.AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Any condition")
                    primary: AppContext.nlq && (AppContext.nlq.match === Library.SmartMatch.Any || AppContext.nlq.match === 1)
                    onClicked: {
                        matchEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setMatch(Library.SmartMatch.Any)
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: entityEditPopup
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        width: Math.min(360, Overlay.overlay ? Overlay.overlay.width - 40 : 360)
        padding: Theme.spacingMedium
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        function openDialog() {
            open()
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Result Type")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            RowLayout {
                spacing: Theme.spacingSmall
                Layout.fillWidth: true

                Controls.AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Tracks")
                    primary: AppContext.nlq && AppContext.nlq.entity === 0
                    onClicked: {
                        entityEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setEntity(0)
                        }
                    }
                }

                Controls.AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Albums")
                    primary: AppContext.nlq && AppContext.nlq.entity === 1
                    onClicked: {
                        entityEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setEntity(1)
                        }
                    }
                }

                Controls.AppButton {
                    Layout.fillWidth: true
                    text: qsTr("Artists")
                    primary: AppContext.nlq && AppContext.nlq.entity === 2
                    onClicked: {
                        entityEditPopup.close()
                        if (AppContext.nlq) {
                            AppContext.nlq.setEntity(2)
                        }
                    }
                }
            }
        }
    }

    // --- Main Content Layout ---

    contentItem: ColumnLayout {
        id: mainLayout
        spacing: Theme.spacingSmall

        // 1. Search / Ask Input Box
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Controls.AppTextField {
                id: inputField
                Layout.fillWidth: true
                placeholderText: (AppContext.nlq && AppContext.nlq.hasConversation)
                    ? qsTr("Refine the results…")
                    : qsTr("Ask about your library…")
                enabled: true

                Keys.onReturnPressed: (event) => {
                    if (text.trim().length > 0 && AppContext.nlq) {
                        AppContext.nlq.submit(text.trim())
                        text = ""
                    }
                    event.accepted = true
                }
                Keys.onEnterPressed: (event) => {
                    if (text.trim().length > 0 && AppContext.nlq) {
                        AppContext.nlq.submit(text.trim())
                        text = ""
                    }
                    event.accepted = true
                }
                Keys.onEscapePressed: (event) => {
                    root.close()
                    event.accepted = true
                }
                Keys.onDownPressed: (event) => {
                    if (resultListView.visible && resultListView.count > 0) {
                        resultListView.forceActiveFocus()
                        if (resultListView.currentIndex < 0) {
                            resultListView.currentIndex = 0
                        }
                        event.accepted = true
                    }
                }
            }

            Controls.AppButton {
                visible: AppContext.nlq && AppContext.nlq.hasConversation
                text: qsTr("New")
                onClicked: {
                    if (AppContext.nlq) {
                        AppContext.nlq.newConversation()
                    }
                    inputField.text = ""
                    inputField.forceActiveFocus()
                }
            }
        }

        // 2. Status Row: Busy indicator, Error text, or unconfigured warning
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: (AppContext.nlq && AppContext.nlq.state === NlqController.Interpreting)
                || (AppContext.nlq && AppContext.nlq.state === NlqController.Failed)
                || (AppContext.nlq && AppContext.nlq.offline)

            BusyIndicator {
                visible: AppContext.nlq && AppContext.nlq.state === NlqController.Interpreting
                running: visible
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }

            Label {
                visible: AppContext.nlq && AppContext.nlq.state === NlqController.Interpreting
                text: qsTr("Understanding…")
                color: Theme.accent
                font.pixelSize: Theme.fontSizeSmall
                Layout.fillWidth: true
            }

            Controls.AppButton {
                visible: AppContext.nlq && AppContext.nlq.state === NlqController.Interpreting
                text: qsTr("Cancel")
                onClicked: {
                    if (AppContext.nlq) {
                        AppContext.nlq.cancel()
                    }
                }
            }

            Label {
                visible: AppContext.nlq && AppContext.nlq.state === NlqController.Failed
                text: AppContext.nlq ? AppContext.nlq.errorText : ""
                color: Theme.errorText
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Label {
                visible: AppContext.nlq && AppContext.nlq.offline && AppContext.nlq.state !== NlqController.Failed
                text: qsTr("Offline mode — AI service not configured; using simple keyword rules.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        // 3. Explanation
        Label {
            visible: AppContext.nlq && AppContext.nlq.explanation.length > 0
            text: AppContext.nlq ? AppContext.nlq.explanation : ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeNormal
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // 4. Disambiguation (NeedsChoice)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: AppContext.nlq && AppContext.nlq.state === NlqController.NeedsChoice
                && AppContext.nlq.clarification && AppContext.nlq.clarification.mention

            Label {
                text: qsTr("Which \"%1\" did you mean?").arg(
                    (AppContext.nlq && AppContext.nlq.clarification)
                        ? AppContext.nlq.clarification.mention : "")
                font.bold: true
                color: Theme.text
                font.pixelSize: Theme.fontSizeNormal
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Repeater {
                    model: (AppContext.nlq && AppContext.nlq.clarification)
                        ? AppContext.nlq.clarification.candidates : []

                    delegate: Controls.AppButton {
                        text: qsTr("%1 · %n track(s)", "", modelData.trackCount || 0).arg(modelData.name || "")
                        onClicked: {
                            if (AppContext.nlq) {
                                AppContext.nlq.chooseCandidate(index)
                            }
                        }
                    }
                }
            }
        }

        // 5. Chips Flow (Condition labels)
        Flow {
            id: chipsFlow
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: AppContext.nlq && AppContext.nlq.chips.length > 0

            Repeater {
                model: AppContext.nlq ? AppContext.nlq.chips : []

                delegate: Rectangle {
                    id: chipDelegate
                    required property int index
                    required property var modelData

                    implicitWidth: chipRow.implicitWidth + Theme.spacingSmall * 2
                    implicitHeight: Theme.controlHeight - 6
                    radius: Theme.radiusMedium
                    color: chipMouseArea.containsMouse ? Theme.itemHover : Theme.surfaceVariant
                    border.color: Theme.divider
                    border.width: 1

                    RowLayout {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: Theme.spacingTiny

                        Label {
                            text: chipDelegate.modelData ? chipDelegate.modelData.text : ""
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.text
                        }

                        // Remove 'x' button for Condition and PlayWindow
                        Label {
                            visible: chipDelegate.modelData && (
                                chipDelegate.modelData.kind === NlqController.Condition
                                || chipDelegate.modelData.kind === 2
                                || chipDelegate.modelData.kind === NlqController.PlayWindow
                                || chipDelegate.modelData.kind === 3
                            )
                            text: "×"
                            font.pixelSize: 14
                            font.bold: true
                            color: removeMouseArea.containsMouse ? Theme.accent : Theme.textSecondary

                            MouseArea {
                                id: removeMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (event) => {
                                    event.accepted = true
                                    if (AppContext.nlq) {
                                        if (chipDelegate.modelData.kind === NlqController.Condition || chipDelegate.modelData.kind === 2) {
                                            AppContext.nlq.removeChip(chipDelegate.modelData.index)
                                        } else if (chipDelegate.modelData.kind === NlqController.PlayWindow || chipDelegate.modelData.kind === 3) {
                                            AppContext.nlq.setPlayWindow("", "")
                                        }
                                    }
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: chipMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.openChipEditor(chipDelegate.modelData)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
            visible: resultContainer.visible
        }

        // 6. Results Container
        Item {
            id: resultContainer
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            Layout.preferredHeight: 320
            visible: AppContext.nlq && AppContext.nlq.state === NlqController.Ready

            // Empty state
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Theme.spacingLarge * 2, 520)
                spacing: Theme.spacingMedium
                visible: AppContext.nlq && AppContext.nlq.rows.length === 0

                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: (AppContext.nlq && AppContext.nlq.emptyHint.length > 0)
                        ? AppContext.nlq.emptyHint
                        : qsTr("No results")
                    font.pixelSize: Theme.fontSizeLarge
                    color: Theme.textSecondary
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.spacingSmall
                    visible: AppContext.nlq && AppContext.nlq.relaxations.length > 0

                    Repeater {
                        model: (AppContext.nlq && AppContext.nlq.relaxations)
                            ? AppContext.nlq.relaxations.slice(0, 3) : []

                        delegate: Controls.AppButton {
                            required property int index
                            required property var modelData
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData ? (modelData.text || "") : ""
                            onClicked: {
                                if (AppContext.nlq) {
                                    AppContext.nlq.applyRelaxation(index)
                                }
                            }
                        }
                    }
                }
            }

            // Results List
            ListView {
                id: resultListView
                anchors.fill: parent
                // 无结果时隐藏，否则会盖住空结果提示并吞掉放宽建议按钮的点击
                visible: count > 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                focus: true
                keyNavigationEnabled: true
                model: (AppContext.nlq && AppContext.nlq.state === NlqController.Ready)
                    ? AppContext.nlq.rows : []

                ScrollBar.vertical: Controls.AppScrollBar {}

                Keys.onReturnPressed: (event) => {
                    if (currentIndex >= 0 && AppContext.nlq) {
                        AppContext.nlq.playRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onEnterPressed: (event) => {
                    if (currentIndex >= 0 && AppContext.nlq) {
                        AppContext.nlq.playRow(currentIndex)
                        event.accepted = true
                    }
                }
                Keys.onEscapePressed: (event) => {
                    root.close()
                    event.accepted = true
                }
                Keys.onUpPressed: (event) => {
                    if (currentIndex === 0) {
                        inputField.forceActiveFocus()
                        event.accepted = true
                    } else {
                        event.accepted = false
                    }
                }

                delegate: Rectangle {
                    id: rowDelegate
                    required property int index
                    required property var modelData

                    width: resultListView.width - (resultListView.ScrollBar.vertical.visible ? resultListView.ScrollBar.vertical.width : 0)
                    height: (AppContext.nlq && AppContext.nlq.entity === 1) ? 48 : Theme.trackRowHeight
                    radius: Theme.radiusSmall
                    color: {
                        if (resultListView.currentIndex === index && resultListView.activeFocus) {
                            return Theme.itemSelected
                        }
                        if (rowMouseArea.containsMouse) {
                            return Theme.itemHover
                        }
                        return "transparent"
                    }

                    // --- TRACK ROW ---
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium
                        visible: AppContext.nlq && AppContext.nlq.entity === 0

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: Theme.radiusSmall
                            color: Theme.surfaceVariant
                            clip: true

                            Image {
                                anchors.fill: parent
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                source: (rowDelegate.modelData && rowDelegate.modelData.coverHash)
                                    ? ("image://cover/" + encodeURIComponent(rowDelegate.modelData.coverHash)) : ""
                                sourceSize: Qt.size(32, 32)
                                visible: status === Image.Ready
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: rowDelegate.modelData ? (rowDelegate.modelData.title || qsTr("Unknown Title")) : ""
                                font.pixelSize: Theme.fontSizeNormal
                                font.bold: true
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: {
                                    if (!rowDelegate.modelData) return ""
                                    const artist = rowDelegate.modelData.artist || qsTr("Unknown Artist")
                                    const album = rowDelegate.modelData.album || ""
                                    return album.length > 0 ? (artist + " · " + album) : artist
                                }
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: rowDelegate.modelData ? (rowDelegate.modelData.durationText || "0:00") : "0:00"
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 50
                        }
                    }

                    // --- ALBUM ROW ---
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium
                        visible: AppContext.nlq && AppContext.nlq.entity === 1

                        Rectangle {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            radius: Theme.radiusSmall
                            color: Theme.surfaceVariant
                            clip: true

                            Image {
                                anchors.fill: parent
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                source: (rowDelegate.modelData && rowDelegate.modelData.coverHash)
                                    ? ("image://cover/" + encodeURIComponent(rowDelegate.modelData.coverHash)) : ""
                                sourceSize: Qt.size(36, 36)
                                visible: status === Image.Ready
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Label {
                                text: rowDelegate.modelData ? (rowDelegate.modelData.title || qsTr("Unknown Album")) : ""
                                font.pixelSize: Theme.fontSizeNormal
                                font.bold: true
                                color: Theme.text
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: rowDelegate.modelData ? (rowDelegate.modelData.artist || qsTr("Unknown Artist")) : ""
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: (rowDelegate.modelData && rowDelegate.modelData.year) ? String(rowDelegate.modelData.year) : ""
                            font.pixelSize: Theme.fontSizeNormal
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 50
                        }
                    }

                    // --- ARTIST ROW ---
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.spacingMedium
                        visible: AppContext.nlq && AppContext.nlq.entity === 2

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: 16
                            color: Theme.surfaceVariant

                            Label {
                                anchors.centerIn: parent
                                text: (rowDelegate.modelData && rowDelegate.modelData.name && rowDelegate.modelData.name.length > 0)
                                    ? rowDelegate.modelData.name.charAt(0).toUpperCase() : "?"
                                font.pixelSize: Theme.fontSizeNormal
                                font.bold: true
                                color: Theme.textSecondary
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: rowDelegate.modelData ? (rowDelegate.modelData.name || qsTr("Unknown Artist")) : ""
                            font.pixelSize: Theme.fontSizeNormal
                            font.bold: true
                            color: Theme.text
                            elide: Text.ElideRight
                        }

                        Label {
                            text: qsTr("%n track(s)", "", Number(rowDelegate.modelData && rowDelegate.modelData.trackCount ? rowDelegate.modelData.trackCount : 0))
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.textSecondary
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    MouseArea {
                        id: rowMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            resultListView.currentIndex = rowDelegate.index
                            resultListView.forceActiveFocus()
                        }
                        onDoubleClicked: {
                            if (AppContext.nlq) {
                                AppContext.nlq.playRow(rowDelegate.index)
                            }
                        }
                    }
                }
            }
        }

        // 7. Bottom Actions Row
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: AppContext.nlq && AppContext.nlq.state === NlqController.Ready
                && AppContext.nlq.rows.length > 0

            Controls.AppButton {
                text: qsTr("Play all")
                primary: true
                onClicked: {
                    if (AppContext.nlq) {
                        AppContext.nlq.playAll()
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Add to queue")
                onClicked: {
                    if (AppContext.nlq) {
                        AppContext.nlq.enqueueAll()
                    }
                }
            }

            Controls.AppButton {
                text: qsTr("Save as playlist…")
                onClicked: {
                    playlistNameDialog.openWithText(
                        (AppContext.nlq && AppContext.nlq.explanation.length > 0)
                            ? AppContext.nlq.explanation : qsTr("Query Result")
                    )
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
