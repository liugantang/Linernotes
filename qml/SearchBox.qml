// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    implicitWidth: 280
    implicitHeight: Theme.controlHeight

    signal focusTrackListRequested()

    function focusInput() {
        textField.forceActiveFocus()
        textField.selectAll()
    }

    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: root.focusInput()
    }

    Connections {
        target: AppContext.search
        function onQueryChanged() {
            if (AppContext.search && textField.text !== AppContext.search.query) {
                textField.text = AppContext.search.query
            }
        }
    }

    Controls.AppTextField {
        id: textField
        anchors.fill: parent
        leftPadding: Theme.spacingSmall + Theme.iconSize + Theme.spacingSmall
        rightPadding: clearButton.visible ? (Theme.spacingSmall + Theme.iconSize + Theme.spacingSmall) : Theme.spacingSmall
        placeholderText: qsTr("Search tracks, albums, artists")
        text: AppContext.search ? AppContext.search.query : ""

        onTextEdited: {
            if (AppContext.search) {
                AppContext.search.query = text
            }
        }

        Keys.onEscapePressed: (event) => {
            if (AppContext.search) {
                AppContext.search.clear()
            }
            textField.text = ""
            textField.focus = false
            event.accepted = true
        }

        Keys.onDownPressed: (event) => {
            if (AppContext.search && AppContext.search.active) {
                root.focusTrackListRequested()
                event.accepted = true
            }
        }
    }

    IconImage {
        id: searchIcon
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.iconSize
        height: Theme.iconSize
        source: "icons/search.svg"
        color: Theme.textSecondary
    }

    Controls.IconButton {
        id: clearButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingTiny
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: Theme.controlHeight - 8
        implicitHeight: Theme.controlHeight - 8
        visible: textField.text.length > 0
        icon.source: "icons/x.svg"
        icon.width: Theme.iconSize - 2
        icon.height: Theme.iconSize - 2
        icon.color: Theme.textSecondary
        toolTip: qsTr("Clear")
        onClicked: {
            if (AppContext.search) {
                AppContext.search.clear()
            }
            textField.text = ""
            textField.forceActiveFocus()
        }
    }
}
