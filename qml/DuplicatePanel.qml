// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

Item {
    id: root

    readonly property bool hasDuplicates: typeof AppContext !== "undefined" && AppContext && AppContext.duplicates
    readonly property bool isBusy: hasDuplicates && AppContext.duplicates.busy
    readonly property int groupCount: hasDuplicates ? AppContext.duplicates.groupCount : 0
    readonly property int recommendedExtraFiles: hasDuplicates ? AppContext.duplicates.recommendedExtraFiles : 0
    readonly property string lastError: hasDuplicates ? AppContext.duplicates.lastError : ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMedium
        spacing: Theme.spacingMedium

        // Top Header
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Extra copies are moved to the system trash; play history, favorites and playlists move to the kept copy.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeNormal
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Keep all recommended")
                primary: true
                enabled: !root.isBusy && root.groupCount > 0 && root.recommendedExtraFiles > 0
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                onClicked: confirmKeepAllDialog.open()
            }
        }

        // Error banner
        Rectangle {
            Layout.fillWidth: true
            visible: root.lastError.length > 0
            height: visible ? (errorLabel.implicitHeight + Theme.spacingSmall * 2) : 0
            color: Theme.errorBackground
            border.color: Theme.errorBorder
            border.width: 1
            radius: Theme.radiusSmall

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                spacing: Theme.spacingSmall

                Label {
                    id: errorLabel
                    text: root.lastError
                    color: Theme.errorText
                    font.pixelSize: Theme.fontSizeNormal
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        // Busy status indicator
        RowLayout {
            Layout.fillWidth: true
            visible: root.isBusy
            spacing: Theme.spacingSmall

            BusyIndicator {
                running: root.isBusy
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }

            Label {
                text: qsTr("Processing duplicates...")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeNormal
            }
        }

        // Empty state
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.isBusy && root.groupCount === 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingMedium
                width: Math.min(parent.width - Theme.spacingLarge * 2, 440)

                IconImage {
                    source: "icons/sparkles.svg"
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    sourceSize: Qt.size(48, 48)
                    color: Theme.textSecondary
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: qsTr("No duplicates found. Run \"Find duplicate songs\" above.")
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
            }
        }

        // Section ListView
        ListView {
            id: sectionListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingMedium
            visible: root.groupCount > 0
            model: root.hasDuplicates ? AppContext.duplicates.sectionModel : null

            Controls.AppScrollBar.vertical: Controls.AppScrollBar {}

            delegate: Rectangle {
                id: sectionCard
                width: sectionListView.width
                implicitHeight: cardLayout.implicitHeight + Theme.spacingMedium * 2
                color: Theme.surface
                border.color: Theme.divider
                border.width: 1
                radius: Theme.radiusMedium

                property bool detailsExpanded: false
                // 在内层 Repeater 里 model / modelData 指向的是专辑、组或成员，节的数据在这里取一次
                readonly property int sectionIdx: model.sectionIndex
                readonly property var albumsData: model.albums
                readonly property var groupsData: model.groups

                ColumnLayout {
                    id: cardLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    // Section Title Row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Label {
                            text: model.title
                            font.pixelSize: Theme.fontSizeLarge
                            font.bold: true
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            color: Theme.surfaceVariant
                            radius: Theme.radiusSmall
                            implicitWidth: countBadgeLabel.implicitWidth + Theme.spacingSmall * 2
                            implicitHeight: countBadgeLabel.implicitHeight + Theme.spacingTiny * 2

                            Label {
                                id: countBadgeLabel
                                anchors.centerIn: parent
                                text: qsTr("%n duplicate(s)", "", model.groupCount)
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.textSecondary
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.divider
                    }

                    // Albums List
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: sectionCard.albumsData

                            delegate: Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: albumRowLayout.implicitHeight + Theme.spacingSmall * 2
                                color: Theme.surfaceVariant
                                radius: Theme.radiusSmall
                                border.color: modelData.isRecommended ? Theme.accent : "transparent"
                                border.width: modelData.isRecommended ? 1 : 0

                                RowLayout {
                                    id: albumRowLayout
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    spacing: Theme.spacingMedium

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingTiny

                                        RowLayout {
                                            spacing: Theme.spacingSmall

                                            Label {
                                                text: modelData.title
                                                font.pixelSize: Theme.fontSizeNormal
                                                font.bold: true
                                                color: Theme.text
                                                elide: Text.ElideRight
                                            }

                                            Rectangle {
                                                visible: modelData.isRecommended
                                                color: Theme.accent
                                                radius: Theme.radiusSmall
                                                implicitWidth: recBadge.implicitWidth + Theme.spacingSmall
                                                implicitHeight: recBadge.implicitHeight + Theme.spacingTiny

                                                Label {
                                                    id: recBadge
                                                    anchors.centerIn: parent
                                                    text: qsTr("Recommended")
                                                    font.pixelSize: Theme.fontSizeSmall
                                                    font.bold: true
                                                    color: Theme.accentText
                                                }
                                            }
                                        }

                                        RowLayout {
                                            spacing: Theme.spacingMedium

                                            Label {
                                                text: modelData.folder
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSizeSmall
                                                elide: Text.ElideMiddle
                                                Layout.fillWidth: true
                                            }

                                            Label {
                                                text: modelData.format
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSizeSmall
                                                Layout.alignment: Qt.AlignRight
                                            }
                                        }
                                    }

                                    Controls.AppButton {
                                        text: qsTr("Keep this album")
                                        enabled: !root.isBusy
                                        primary: modelData.isRecommended
                                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                        onClicked: {
                                            if (root.hasDuplicates) {
                                                AppContext.duplicates.keepAlbum(sectionCard.sectionIdx, modelData.albumId)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Section Footer / Toggle
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Controls.AppButton {
                            text: qsTr("Not duplicates")
                            enabled: !root.isBusy
                            onClicked: {
                                if (root.hasDuplicates) {
                                    AppContext.duplicates.dismissSection(sectionCard.sectionIdx)
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Controls.AppButton {
                            text: sectionCard.detailsExpanded ? qsTr("Hide details") : qsTr("Show details")
                            onClicked: sectionCard.detailsExpanded = !sectionCard.detailsExpanded
                        }
                    }

                    // Detailed Track List (Expanded)
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: sectionCard.detailsExpanded
                        spacing: Theme.spacingMedium

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: Theme.divider
                        }

                        Repeater {
                            model: sectionCard.detailsExpanded ? sectionCard.groupsData : []

                            delegate: Rectangle {
                                id: groupRow
                                readonly property var groupId: modelData.groupId
                                Layout.fillWidth: true
                                implicitHeight: groupDetailsLayout.implicitHeight + Theme.spacingSmall * 2
                                color: Theme.background
                                radius: Theme.radiusSmall
                                border.color: Theme.divider
                                border.width: 1

                                ColumnLayout {
                                    id: groupDetailsLayout
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    spacing: Theme.spacingSmall

                                    Repeater {
                                        model: modelData.members

                                        delegate: RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Theme.spacingSmall

                                            Label {
                                                text: modelData.title
                                                font.pixelSize: Theme.fontSizeNormal
                                                font.bold: true
                                                color: Theme.text
                                                elide: Text.ElideRight
                                                Layout.preferredWidth: 160
                                            }

                                            Label {
                                                text: modelData.format
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSizeSmall
                                                Layout.preferredWidth: 100
                                            }

                                            Label {
                                                text: modelData.durationText
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSizeSmall
                                                Layout.preferredWidth: 60
                                            }

                                            Label {
                                                text: modelData.path
                                                color: Theme.textSecondary
                                                font.pixelSize: Theme.fontSizeSmall
                                                elide: Text.ElideMiddle
                                                Layout.fillWidth: true
                                            }

                                            Rectangle {
                                                visible: modelData.recommended
                                                color: Theme.surfaceVariant
                                                radius: Theme.radiusSmall
                                                implicitWidth: memberRecLabel.implicitWidth + Theme.spacingSmall
                                                implicitHeight: memberRecLabel.implicitHeight + Theme.spacingTiny

                                                Label {
                                                    id: memberRecLabel
                                                    anchors.centerIn: parent
                                                    text: qsTr("Recommended")
                                                    font.pixelSize: Theme.fontSizeSmall
                                                    font.bold: true
                                                    color: Theme.accent
                                                }
                                            }

                                            Controls.AppButton {
                                                text: qsTr("Keep")
                                                enabled: !root.isBusy
                                                primary: modelData.recommended
                                                onClicked: {
                                                    if (root.hasDuplicates) {
                                                        AppContext.duplicates.keepTrack(groupRow.groupId, modelData.trackId)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: confirmKeepAllDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay

        implicitWidth: 380
        implicitHeight: confirmLayout.implicitHeight + topPadding + bottomPadding
        padding: Theme.spacingMedium

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.radiusMedium
        }

        contentItem: ColumnLayout {
            id: confirmLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Keep all recommended")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Move %n extra file(s) to the trash?", "", root.recommendedExtraFiles)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
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
                    onClicked: confirmKeepAllDialog.close()
                }

                Controls.AppButton {
                    text: qsTr("Keep")
                    primary: true
                    onClicked: {
                        confirmKeepAllDialog.close()
                        if (root.hasDuplicates) {
                            AppContext.duplicates.keepAllRecommended()
                        }
                    }
                }
            }
        }
    }
}
