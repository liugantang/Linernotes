// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

ScrollBar {
    id: control

    implicitWidth: orientation === Qt.Vertical ? 12 : 0
    implicitHeight: orientation === Qt.Horizontal ? 12 : 0

    padding: 2

    policy: size < 1.0 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Vertical
        ? Math.min(1.0, 32 / Math.max(1, height))
        : Math.min(1.0, 32 / Math.max(1, width))

    contentItem: Rectangle {
        implicitWidth: (control.hovered || control.pressed) ? 8 : 6
        implicitHeight: (control.hovered || control.pressed) ? 8 : 6
        radius: Math.min(width, height) / 2
        color: (control.hovered || control.pressed) ? Theme.scrollBarThumbActive : Theme.scrollBarThumb

        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }

        Behavior on implicitWidth {
            NumberAnimation {
                duration: 100
            }
        }

        Behavior on implicitHeight {
            NumberAnimation {
                duration: 100
            }
        }
    }

    background: Rectangle {
        color: "transparent"
    }
}
