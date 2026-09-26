// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Linernotes
import "../controls" as Controls

Item {
    id: root

    property var selectedArtistId: 0

    function openArtist(artistId) {
        root.selectedArtistId = artistId
        for (let i = 0; i < artistModel.count; ++i) {
            const id = artistModel.data(artistModel.index(i, 0), ArtistListModel.ArtistIdRole)
            if (id === artistId) {
                artistListView.currentIndex = i
                break
            }
        }
    }

    ArtistListModel {
        id: artistModel
        context: AppContext
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left: Artist List
        Rectangle {
            Layout.preferredWidth: Theme.artistListWidth
            Layout.fillHeight: true
            color: Theme.surface

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
                        text: qsTr("Artists")
                        font.pixelSize: Theme.fontSizeLarge
                        font.bold: true
                        color: Theme.text
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Controls.AppButton {
                        text: qsTr("Loved")
                        icon.source: artistModel.favoritesOnly ? "../icons/heart-filled.svg" : "../icons/heart.svg"
                        icon.color: artistModel.favoritesOnly ? Theme.favorite : Theme.text
                        checked: artistModel.favoritesOnly
                        onClicked: {
                            artistModel.favoritesOnly = !artistModel.favoritesOnly
                        }
                    }

                    Label {
                        text: qsTr("%n artist(s)", "", artistModel.count)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.textSecondary
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.divider
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        anchors.centerIn: parent
                        visible: artistModel.count === 0
                        text: artistModel.favoritesOnly ? qsTr("No loved artists yet") : qsTr("No artists in library")
                        font.pixelSize: Theme.fontSizeNormal
                        color: Theme.textSecondary
                    }

                    ListView {
                        id: artistListView
                        anchors.fill: parent
                        clip: true
                        focus: true
                        reuseItems: true
                        keyNavigationEnabled: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: artistModel

                        onCurrentIndexChanged: {
                            if (currentIndex >= 0 && currentIndex < artistModel.count) {
                                const id = artistModel.data(artistModel.index(currentIndex, 0), ArtistListModel.ArtistIdRole)
                                if (id) {
                                    root.selectedArtistId = id
                                }
                            }
                        }

                        delegate: Rectangle {
                            id: artistDelegate
                            required property int index
                            required property var artistId
                            required property string name
                            required property int trackCount
                            required property int albumCount
                            required property string coverHash

                            width: ListView.view.width
                            height: 60
                            color: {
                                if (root.selectedArtistId === artistDelegate.artistId) {
                                    return Theme.itemSelected
                                }
                                if (delegateMouseArea.containsMouse) {
                                    return Theme.itemHover
                                }
                                return "transparent"
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingMedium
                                anchors.rightMargin: Theme.spacingMedium
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    Layout.preferredWidth: Theme.artistAvatarSizeSmall
                                    Layout.preferredHeight: Theme.artistAvatarSizeSmall
                                    radius: Theme.artistAvatarSizeSmall / 2
                                    color: Theme.surfaceVariant
                                    clip: true
                                    layer.enabled: true
                                    layer.effect: MultiEffect {
                                        maskEnabled: true
                                        maskSource: artistListAvatarMask
                                    }

                                    Item {
                                        id: artistListAvatarMask
                                        anchors.fill: parent
                                        layer.enabled: true
                                        visible: false

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: width / 2
                                            color: "black"
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: avatarImg.status !== Image.Ready || !artistDelegate.coverHash
                                        text: (artistDelegate.name && artistDelegate.name.length > 0)
                                            ? artistDelegate.name.charAt(0).toUpperCase() : "?"
                                        font.pixelSize: 18
                                        font.bold: true
                                        color: Theme.textSecondary
                                    }

                                    Image {
                                        id: avatarImg
                                        anchors.fill: parent
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectCrop
                                        source: artistDelegate.coverHash
                                            ? ("image://cover/" + encodeURIComponent(artistDelegate.coverHash)) : ""
                                        sourceSize: Qt.size(Theme.artistAvatarSizeSmall, Theme.artistAvatarSizeSmall)
                                        visible: status === Image.Ready
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        text: artistDelegate.name || qsTr("Unknown Artist")
                                        font.pixelSize: Theme.fontSizeNormal
                                        font.bold: root.selectedArtistId === artistDelegate.artistId
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: qsTr("%1 albums · %2 tracks")
                                            .arg(artistDelegate.albumCount)
                                            .arg(artistDelegate.trackCount)
                                        font.pixelSize: Theme.fontSizeSmall
                                        color: Theme.textSecondary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                            }

                            MouseArea {
                                id: delegateMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    artistListView.currentIndex = artistDelegate.index
                                    root.selectedArtistId = artistDelegate.artistId
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            color: Theme.divider
        }

        // Right: Selected Artist Details
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Label {
                anchors.centerIn: parent
                visible: !root.selectedArtistId || artistModel.count === 0
                text: qsTr("Select an artist to view details")
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.textSecondary
            }

            ArtistDetail {
                anchors.fill: parent
                visible: root.selectedArtistId > 0 && artistModel.count > 0
                artistId: root.selectedArtistId
                onAlbumSelected: (albumId) => {
                    window.openAlbum(albumId)
                }
            }
        }
    }
}
