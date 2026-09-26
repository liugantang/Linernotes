// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes
import "../controls" as Controls

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight
            Layout.leftMargin: Theme.spacingMedium
            Layout.rightMargin: Theme.spacingMedium
            spacing: Theme.spacingSmall

            Label {
                text: qsTr("Library")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
            }

            Label {
                text: "·"
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.textSecondary
            }

            Label {
                text: qsTr("%n track(s)", "", trackTable.count)
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
            }

            Item {
                Layout.fillWidth: true
            }

            Controls.AppButton {
                text: qsTr("Loved")
                icon.source: trackTable.model.favoritesOnly ? "../icons/heart-filled.svg" : "../icons/heart.svg"
                icon.color: trackTable.model.favoritesOnly ? Theme.favorite : Theme.text
                checked: trackTable.model.favoritesOnly
                onClicked: {
                    trackTable.model.favoritesOnly = !trackTable.model.favoritesOnly
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        TrackTable {
            id: trackTable
            Layout.fillWidth: true
            Layout.fillHeight: true
            emptyText: trackTable.model.favoritesOnly ? qsTr("No loved tracks yet") : qsTr("No tracks in library")
        }
    }
}
