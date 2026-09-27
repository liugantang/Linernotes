// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls
import Linernotes

Item {
    id: root

    function openAlbum(albumId) {
        while (stackView.depth > 1) {
            stackView.pop()
        }
        stackView.push(detailComponent, { albumId: albumId })
    }

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: gridComponent
    }

    Component {
        id: gridComponent

        AlbumGrid {
            onAlbumClicked: (albumId) => {
                stackView.push(detailComponent, { albumId: albumId })
            }
        }
    }

    Component {
        id: detailComponent

        AlbumDetail {
            onBackRequested: {
                stackView.pop()
            }
        }
    }
}
