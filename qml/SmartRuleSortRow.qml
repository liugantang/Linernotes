// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root

    property int sortKey: Library.TrackSortKey.Default
    property int sortOrder: Qt.AscendingOrder
    property bool limitEnabled: false
    property int limitCount: 50

    spacing: Theme.spacingSmall

    readonly property var sortKeysModel: AppContext.playlists
        ? AppContext.playlists.smartSortKeys().map(function(k) {
            return {
                text: AppContext.playlists.sortKeyLabel(k),
                value: k
            }
        })
        : []

    readonly property var sortOrdersModel: [
        { text: qsTr("Ascending"), value: Qt.AscendingOrder },
        { text: qsTr("Descending"), value: Qt.DescendingOrder }
    ]

    function syncCombos() {
        if (sortKeyCombo && sortKeysModel && sortKeysModel.length > 0) {
            sortKeyCombo.currentIndex = sortKeysModel.findIndex(m => m.value === root.sortKey)
        }
        if (sortOrderCombo && sortOrdersModel && sortOrdersModel.length > 0) {
            sortOrderCombo.currentIndex = sortOrdersModel.findIndex(m => m.value === root.sortOrder)
        }
    }

    onSortKeyChanged: {
        syncCombos()
    }

    onSortOrderChanged: {
        syncCombos()
    }

    Component.onCompleted: {
        syncCombos()
    }

    // Sort Row
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Label {
            text: qsTr("Sort by:")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.text
            Layout.preferredWidth: 60
        }

        Controls.AppComboBox {
            id: sortKeyCombo
            Layout.preferredWidth: 140
            textRole: "text"
            valueRole: "value"
            model: root.sortKeysModel

            onModelChanged: {
                root.syncCombos()
            }

            Component.onCompleted: {
                root.syncCombos()
            }

            onActivated: (index) => {
                if (model[index]) {
                    root.sortKey = model[index].value
                }
            }
        }

        Controls.AppComboBox {
            id: sortOrderCombo
            Layout.preferredWidth: 130
            textRole: "text"
            valueRole: "value"
            model: root.sortOrdersModel

            onModelChanged: {
                root.syncCombos()
            }

            Component.onCompleted: {
                root.syncCombos()
            }

            onActivated: (index) => {
                if (model[index]) {
                    root.sortOrder = model[index].value
                }
            }
        }
    }

    // Limit Row
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Controls.AppCheckBox {
            id: limitCheck
            text: qsTr("Limit to")
            checked: root.limitEnabled
            onCheckedChanged: {
                root.limitEnabled = checked
            }
        }

        Controls.AppTextField {
            id: limitField
            Layout.preferredWidth: 80
            enabled: limitCheck.checked
            text: String(root.limitCount)
            inputMethodHints: Qt.ImhDigitsOnly
            onTextEdited: {
                root.limitCount = Number(text) || 50
            }
        }

        Label {
            //: Unit label after number input in "Limit to [N] tracks"
            text: qsTr("tracks")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
        }
    }
}
