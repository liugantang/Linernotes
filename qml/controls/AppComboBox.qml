// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import Linernotes

ComboBox {
    id: control
    
    implicitHeight: Theme.controlHeight
    
    font.pixelSize: Theme.fontSizeNormal
    palette.text: Theme.text
    palette.highlightedText: Theme.text
    
    background: Rectangle {
        implicitWidth: 120
        color: control.down ? Theme.itemSelected : (control.hovered ? Theme.hoverOverlay : "transparent")
        border.color: Theme.divider
        border.width: 1
        radius: Theme.radiusMedium
        
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: control.visualFocus ? Theme.focusRing : "transparent"
            border.width: 2
            radius: Theme.radiusMedium + 2
            visible: control.visualFocus
        }
    }

    contentItem: Text {
        leftPadding: Theme.spacingSmall
        rightPadding: control.indicator.width + control.spacing
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Canvas {
        id: canvas
        x: control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 12
        height: 8
        contextType: "2d"

        Connections {
            target: control
            function onPressedChanged() { canvas.requestPaint(); }
        }

        onPaint: {
            var context = getContext("2d");
            context.reset();
            context.moveTo(0, 0);
            context.lineTo(width, 0);
            context.lineTo(width / 2, height);
            context.closePath();
            context.fillStyle = Theme.text;
            context.fill();
        }
    }

    popup: Popup {
        y: control.height + Theme.spacingTiny
        width: control.width
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: Theme.spacingTiny

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            color: Theme.surface
            radius: Theme.radiusMedium
            border.color: Theme.divider
            border.width: 1
        }
    }

    delegate: ItemDelegate {
        width: control.width - Theme.spacingTiny * 2
        height: Theme.controlHeight
        
        contentItem: Text {
            text: control.textAt(index)
            color: control.currentIndex === index ? Theme.accent : Theme.text
            font: control.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        
        background: Rectangle {
            color: parent.highlighted ? Theme.hoverOverlay : "transparent"
            radius: Theme.radiusSmall
        }
    }
}
