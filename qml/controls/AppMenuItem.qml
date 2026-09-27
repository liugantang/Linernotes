// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes

MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             Theme.controlHeight)

    leftPadding: Theme.spacingSmall
    rightPadding: Theme.spacingSmall
    topPadding: Theme.spacingTiny
    bottomPadding: Theme.spacingTiny
    spacing: Theme.spacingSmall

    font.pixelSize: Theme.fontSizeNormal

    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: control.enabled ? Theme.text : Theme.textSecondary

    // Checkable checkmark indicator
    indicator: Item {
        implicitWidth: Theme.iconSize
        implicitHeight: Theme.iconSize
        visible: control.checkable
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter

        Label {
            anchors.centerIn: parent
            text: "✓"
            font.pixelSize: Theme.fontSizeNormal
            font.bold: true
            color: control.enabled ? (control.highlighted ? Theme.accent : Theme.text) : Theme.textSecondary
            visible: control.checked
        }
    }

    // Submenu arrow indicator
    arrow: Canvas {
        x: control.width - width - control.rightPadding
        y: (control.height - height) / 2
        width: 8
        height: 12
        visible: control.subMenu !== null
        contextType: "2d"

        Connections {
            target: control
            function onHighlightedChanged() { arrow.requestPaint(); }
            function onEnabledChanged() { arrow.requestPaint(); }
        }

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.moveTo(0, 0);
            ctx.lineTo(width, height / 2);
            ctx.lineTo(0, height);
            ctx.closePath();
            ctx.fillStyle = control.enabled ? (control.highlighted ? Theme.accent : Theme.textSecondary) : Theme.textSecondary;
            ctx.fill();
        }
    }

    contentItem: RowLayout {
        spacing: control.spacing
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: control.leftPadding + (control.checkable ? (Theme.iconSize + control.spacing) : 0)
        anchors.rightMargin: control.rightPadding + (control.subMenu !== null ? (8 + control.spacing) : 0)

        Label {
            text: control.text
            font: control.font
            color: control.enabled ? Theme.text : Theme.textSecondary
            opacity: control.enabled ? 1.0 : 0.5
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
        }
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: Theme.controlHeight
        color: control.highlighted ? Theme.hoverOverlay : "transparent"
        radius: Theme.radiusSmall
    }
}
