// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Linernotes

Rectangle {
    id: trackDelegate

    required property int index
    required property var trackId
    required property var trackNumber
    required property string title
    required property string artist
    required property string durationText
    required property var albumDetail
    required property var contextMenu
    required property int versionType
    required property string titleTranslated
    property real trailingInset: 0

    width: ListView.view ? ListView.view.width : parent.width
    height: Theme.trackRowHeight
    radius: Theme.coverBorderRadius
    color: {
        if (albumDetail.currentIndex === trackDelegate.index && albumDetail.activeFocus) {
            return Theme.itemSelected
        }
        if (rowMouseArea.containsMouse) {
            return Theme.itemHover
        }
        return "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: trackDelegate.trailingInset + Theme.spacingMedium
        spacing: Theme.spacingMedium

        Label {
            text: (trackDelegate.trackNumber !== undefined && trackDelegate.trackNumber !== null)
                ? trackDelegate.trackNumber : (trackDelegate.index + 1)
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            Layout.preferredWidth: 32
            horizontalAlignment: Text.AlignHCenter
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                id: titleRowLayout
                Layout.fillWidth: true
                spacing: Theme.spacingTiny

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    verticalAlignment: Text.AlignVCenter
                    text: trackDelegate.title || qsTr("Unknown Title")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeNormal
                }

                Label {
                    visible: AppContext.settings && AppContext.settings.showTranslations && trackDelegate.titleTranslated.length > 0
                    text: trackDelegate.titleTranslated
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    verticalAlignment: Text.AlignVCenter
                    Layout.maximumWidth: titleRowLayout.width * 0.4
                }

                VersionBadge {
                    versionType: trackDelegate.versionType
                    Layout.alignment: Qt.AlignVCenter
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            Label {
                visible: trackDelegate.artist.length > 0 &&
                    trackDelegate.artist !== (albumDetail.info ? albumDetail.info.albumArtist : "")
                text: trackDelegate.artist
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.textSecondary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Label {
            text: trackDelegate.durationText
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            Layout.preferredWidth: 60
            horizontalAlignment: Text.AlignRight
        }
    }

    MouseArea {
        id: rowMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (mouse) => {
            trackDelegate.ListView.view.forceActiveFocus()
            albumDetail.currentIndex = trackDelegate.index
            if (mouse.button === Qt.RightButton && trackDelegate.trackId > 0) {
                trackDelegate.contextMenu.popupFor([trackDelegate.trackId])
            }
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                albumDetail.playTrackAt(trackDelegate.index)
            }
        }
    }
}
