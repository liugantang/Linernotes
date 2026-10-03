// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    readonly property bool hasTrack: !!AppContext.nowPlaying && AppContext.nowPlaying.hasTrack
    readonly property bool loading: !!AppContext.guide && AppContext.guide.loading
    readonly property bool failed: !!AppContext.guide && AppContext.guide.failed
    readonly property var cards: (AppContext.guide && AppContext.guide.cards) ? AppContext.guide.cards : []
    readonly property int cardCount: cards.length

    Item {
        anchors.fill: parent

        // 1. Nothing playing state
        Item {
            anchors.centerIn: parent
            visible: !root.hasTrack

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Nothing playing.")
                    font.pixelSize: Theme.fontSizeLarge
                    font.bold: true
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // 2. Loading state with no cards
        Item {
            anchors.centerIn: parent
            visible: root.hasTrack && root.loading && root.cardCount === 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("Looking up…")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // 3. Failed state with no cards
        Item {
            anchors.centerIn: parent
            visible: root.hasTrack && !root.loading && root.failed && root.cardCount === 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingMedium

                Label {
                    text: qsTr("Couldn't reach Wikipedia.")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                }

                Controls.AppButton {
                    text: qsTr("Retry")
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: {
                        if (AppContext.guide) {
                            AppContext.guide.retry()
                        }
                    }
                }
            }
        }

        // 4. Finished with no cards
        Item {
            anchors.centerIn: parent
            visible: root.hasTrack && !root.loading && !root.failed && root.cardCount === 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Label {
                    text: qsTr("No information found for this track.")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // 5. Card list
        ListView {
            id: listView
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            visible: root.hasTrack && root.cardCount > 0
            model: root.cards
            spacing: Theme.spacingMedium

            ScrollBar.vertical: Controls.AppScrollBar {}

            footer: Item {
                width: listView.width
                height: Theme.spacingExtraLarge
                visible: root.loading && root.cardCount > 0

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Looking up more…")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.textSecondary
                }
            }

            delegate: Rectangle {
                id: cardDelegate
                required property int index
                required property var modelData

                property bool factsExpanded: false

                width: listView.width
                implicitHeight: cardContent.implicitHeight + Theme.spacingMedium * 2

                color: Theme.surfaceVariant
                radius: Theme.cardBorderRadius
                border.color: Theme.divider
                border.width: 1

                ColumnLayout {
                    id: cardContent
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingSmall

                    // 1. Kind label (small text)
                    Label {
                        text: cardDelegate.modelData.kindLabel || ""
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.accent
                        Layout.fillWidth: true
                    }

                    // 2. Title (bold)
                    Label {
                        text: cardDelegate.modelData.title || ""
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    // 3. Subtitle (secondary color, 1 line elide)
                    Label {
                        visible: !!cardDelegate.modelData.subtitle && cardDelegate.modelData.subtitle.length > 0
                        text: cardDelegate.modelData.subtitle || ""
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        Layout.fillWidth: true
                    }

                    // Divider
                    Rectangle {
                        visible: (!!cardDelegate.modelData.text && cardDelegate.modelData.text.length > 0) || cardDelegate.modelData.writing
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.divider
                        Layout.topMargin: Theme.spacingTiny
                        Layout.bottomMargin: Theme.spacingTiny
                    }

                    // 4. Text (auto wrap; writing and empty -> Writing…)
                    Label {
                        id: bodyTextLabel
                        visible: (!!cardDelegate.modelData.text && cardDelegate.modelData.text.length > 0) || cardDelegate.modelData.writing
                        text: {
                            if (cardDelegate.modelData.writing && (!cardDelegate.modelData.text || cardDelegate.modelData.text.trim().length === 0)) {
                                return qsTr("Writing…")
                            }
                            return cardDelegate.modelData.text || ""
                        }
                        font.pixelSize: Theme.fontSizeNormal
                        color: (cardDelegate.modelData.writing && (!cardDelegate.modelData.text || cardDelegate.modelData.text.trim().length === 0))
                            ? Theme.textSecondary
                            : Theme.text
                        wrapMode: Text.Wrap
                        lineHeight: 1.3
                        Layout.fillWidth: true
                    }

                    // 5. Facts list (2 columns: name secondary / value, max 6, "Show all" toggle)
                    ColumnLayout {
                        id: factsSection
                        visible: !!cardDelegate.modelData.facts && cardDelegate.modelData.facts.length > 0
                        Layout.fillWidth: true
                        spacing: Theme.spacingTiny
                        Layout.topMargin: Theme.spacingTiny

                        readonly property var allFacts: cardDelegate.modelData.facts || []
                        readonly property int factCount: allFacts.length
                        readonly property var displayFacts: (cardDelegate.factsExpanded || factCount <= 6)
                            ? allFacts
                            : allFacts.slice(0, 6)

                        Repeater {
                            model: factsSection.displayFacts
                            RowLayout {
                                id: factRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Label {
                                    text: factRow.modelData.name || ""
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.textSecondary
                                    Layout.preferredWidth: 80
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: factRow.modelData.value || ""
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.text
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        Label {
                            visible: factsSection.factCount > 6
                            text: cardDelegate.factsExpanded
                                ? qsTr("Show less")
                                : qsTr("Show all (%1)").arg(factsSection.factCount)
                            font.pixelSize: Theme.fontSizeSmall
                            color: toggleMouseArea.containsMouse ? Theme.accentHover : Theme.accent
                            font.underline: toggleMouseArea.containsMouse
                            Layout.topMargin: Theme.spacingTiny

                            MouseArea {
                                id: toggleMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: cardDelegate.factsExpanded = !cardDelegate.factsExpanded
                            }
                        }
                    }

                    // 6. Sources list: [1] Wikipedia (zh), [2] Wikidata
                    RowLayout {
                        id: sourcesSection
                        visible: !!cardDelegate.modelData.sources && cardDelegate.modelData.sources.length > 0
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSmall
                        spacing: Theme.spacingMedium

                        Repeater {
                            model: cardDelegate.modelData.sources || []

                            Label {
                                id: sourceItemLabel
                                required property var modelData

                                text: `[${sourceItemLabel.modelData.index}] ${sourceItemLabel.modelData.label}`
                                font.pixelSize: Theme.fontSizeSmall
                                font.underline: srcMouseArea.containsMouse
                                color: srcMouseArea.containsMouse ? Theme.accentHover : Theme.accent

                                MouseArea {
                                    id: srcMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (sourceItemLabel.modelData.url) {
                                            Qt.openUrlExternally(sourceItemLabel.modelData.url)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // 7. Footer: "Written by AI from the sources above" (only when isAi is true)
                    Label {
                        visible: !!cardDelegate.modelData.isAi
                        text: qsTr("Written by AI from the sources above")
                        font.pixelSize: Theme.fontSizeSmall - 2
                        color: Theme.textSecondary
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingTiny
                    }
                }
            }
        }
    }
}
