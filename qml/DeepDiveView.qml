// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    signal backToInfoRequested()

    readonly property var messages: (AppContext.deepDive && AppContext.deepDive.messages) ? AppContext.deepDive.messages : []
    readonly property bool stale: !!AppContext.deepDive && AppContext.deepDive.stale
    readonly property string subject: AppContext.deepDive ? AppContext.deepDive.subject : ""

    function scrollToBottomIfNear() {
        const distFromBottom = listView.contentHeight - (listView.contentY + listView.height)
        if (distFromBottom <= 40) {
            listView.positionViewAtEnd()
        }
    }

    Connections {
        target: AppContext.deepDive
        function onMessagesChanged() {
            Qt.callLater(root.scrollToBottomIfNear)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingSmall

        // Top Header Bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            color: Theme.surfaceVariant
            radius: Theme.radiusMedium
            border.color: Theme.divider
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                Label {
                    text: root.subject
                    font.pixelSize: Theme.fontSizeNormal
                    font.bold: true
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Back to Info")
                    implicitHeight: Theme.controlHeight - 8
                    onClicked: root.backToInfoRequested()
                }

                Controls.IconButton {
                    implicitWidth: Theme.controlHeight - 8
                    implicitHeight: Theme.controlHeight - 8
                    icon.source: "icons/rotate-ccw.svg"
                    toolTip: qsTr("New chat")
                    onClicked: {
                        if (AppContext.deepDive) {
                            AppContext.deepDive.newChat()
                        }
                    }
                }
            }
        }

        // Stale warning banner
        Rectangle {
            visible: root.stale
            Layout.fillWidth: true
            implicitHeight: 36
            color: Theme.isDark ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15) : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
            radius: Theme.radiusSmall
            border.color: Theme.divider
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMedium
                anchors.rightMargin: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Now playing a different song.")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("Start new chat")
                    font.pixelSize: Theme.fontSizeSmall
                    color: newChatMouse.containsMouse ? Theme.accentHover : Theme.accent
                    font.underline: newChatMouse.containsMouse

                    MouseArea {
                        id: newChatMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (AppContext.deepDive) {
                                AppContext.deepDive.newChat()
                            }
                        }
                    }
                }
            }
        }

        // Messages List
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.messages
            spacing: Theme.spacingMedium

            ScrollBar.vertical: Controls.AppScrollBar {}

            onCountChanged: Qt.callLater(root.scrollToBottomIfNear)

            delegate: Item {
                id: msgDelegate
                required property int index
                required property var modelData

                readonly property bool isUser: modelData.role === "user"

                width: listView.width
                implicitHeight: isUser ? userBubble.implicitHeight : assistantCol.implicitHeight

                // User Message
                Rectangle {
                    id: userBubble
                    visible: msgDelegate.isUser
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSmall
                    width: Math.min(listView.width * 0.85, userText.implicitWidth + Theme.spacingMedium * 2)
                    implicitHeight: userText.implicitHeight + Theme.spacingSmall * 2
                    color: Theme.surfaceVariant
                    radius: Theme.cardBorderRadius
                    border.color: Theme.divider
                    border.width: 1

                    TextEdit {
                        id: userText
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        text: msgDelegate.modelData.text || ""
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.text
                        wrapMode: Text.Wrap
                        readOnly: true
                        selectByMouse: true
                        selectedTextColor: Theme.accentText
                        selectionColor: Theme.accent
                    }
                }

                // Assistant Message
                ColumnLayout {
                    id: assistantCol
                    visible: !msgDelegate.isUser
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    spacing: Theme.spacingTiny

                    // Tools used row (small secondary text)
                    Label {
                        id: toolsLabel
                        visible: !!msgDelegate.modelData.tools && msgDelegate.modelData.tools.length > 0
                        Layout.fillWidth: true
                        text: {
                            if (!msgDelegate.modelData.tools) return ""
                            const toolsText = msgDelegate.modelData.tools.join(" · ")
                            return msgDelegate.modelData.pending ? (toolsText + "…") : toolsText
                        }
                        font.pixelSize: Theme.fontSizeSmall - 1
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }

                    // Thinking state
                    Label {
                        visible: !!msgDelegate.modelData.pending && (!msgDelegate.modelData.text || msgDelegate.modelData.text.length === 0)
                        Layout.fillWidth: true
                        text: qsTr("Thinking…")
                        font.pixelSize: Theme.fontSizeNormal
                        font.italic: true
                        color: Theme.textSecondary
                    }

                    // Answer text (selectable)
                    TextEdit {
                        id: assistantText
                        visible: !!msgDelegate.modelData.text && msgDelegate.modelData.text.length > 0
                        Layout.fillWidth: true
                        text: msgDelegate.modelData.text || ""
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.text
                        wrapMode: Text.Wrap
                        readOnly: true
                        selectByMouse: true
                        selectedTextColor: Theme.accentText
                        selectionColor: Theme.accent
                    }

                    // Sources links (Wikipedia, Wikidata, etc.)
                    Flow {
                        id: sourcesFlow
                        visible: !!msgDelegate.modelData.sources && msgDelegate.modelData.sources.length > 0
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingTiny
                        spacing: Theme.spacingMedium

                        Repeater {
                            model: msgDelegate.modelData.sources || []

                            Label {
                                id: srcItem
                                required property var modelData

                                text: srcItem.modelData.label || ""
                                font.pixelSize: Theme.fontSizeSmall
                                color: srcItemMouse.containsMouse ? Theme.accentHover : Theme.accent
                                font.underline: srcItemMouse.containsMouse

                                MouseArea {
                                    id: srcItemMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (srcItem.modelData.url) {
                                            Qt.openUrlExternally(srcItem.modelData.url)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Unverified marker footnote
                    Label {
                        visible: !!msgDelegate.modelData.hasUnverified
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingTiny
                        text: qsTr("† From the model's own knowledge, not verified")
                        font.pixelSize: Theme.fontSizeSmall - 2
                        color: Theme.textSecondary
                    }

                    // Error text + Retry
                    RowLayout {
                        visible: !!msgDelegate.modelData.error && msgDelegate.modelData.error.length > 0
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingTiny
                        spacing: Theme.spacingSmall

                        Label {
                            text: msgDelegate.modelData.error || ""
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.errorText
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        Controls.AppButton {
                            text: qsTr("Retry")
                            implicitHeight: Theme.controlHeight - 8
                            onClicked: {
                                let prevQuestion = ""
                                if (msgDelegate.index > 0 && root.messages[msgDelegate.index - 1].role === "user") {
                                    prevQuestion = root.messages[msgDelegate.index - 1].text
                                }
                                if (prevQuestion.length > 0 && AppContext.deepDive) {
                                    AppContext.deepDive.ask(prevQuestion)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
