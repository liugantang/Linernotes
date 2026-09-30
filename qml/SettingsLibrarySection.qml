// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root
    spacing: Theme.spacingMedium

    property var deleteTargetId: 0
    property string deleteTargetPath: ""
    property string errorMessage: ""

    function syncArtistNamePreferenceIndex() {
        if (AppContext.settings) {
            const idx = artistNamePrefCombo.indexOfValue(AppContext.settings.artistNamePreference)
            if (idx >= 0 && idx !== artistNamePrefCombo.currentIndex) {
                artistNamePrefCombo.currentIndex = idx
            }
        }
    }

    Component.onCompleted: {
        syncArtistNamePreferenceIndex()
    }

    Connections {
        target: AppContext.settings
        function onArtistNamePreferenceChanged() {
            root.syncArtistNamePreferenceIndex()
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Select Music Folder")
        onAccepted: {
            root.errorMessage = ""
            if (AppContext.libraryRoots) {
                const ok = AppContext.libraryRoots.addRoot(selectedFolder)
                if (!ok) {
                    root.errorMessage = qsTr("Failed to add folder. The folder may not exist or is already in the library.")
                }
            }
        }
    }

    Popup {
        id: confirmDeleteDialog
        modal: true
        focus: true
        dim: true
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        implicitWidth: 380
        implicitHeight: deleteLayout.implicitHeight + topPadding + bottomPadding
        padding: Theme.spacingMedium

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.5)
        }

        background: Rectangle {
            color: Theme.surface
            border.color: Theme.divider
            border.width: 1
            radius: Theme.cardBorderRadius
        }

        contentItem: ColumnLayout {
            id: deleteLayout
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Remove Music Folder")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Remove this folder from the library? Tracks in it will be removed from the library (files on disk are not touched).")
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
                    onClicked: confirmDeleteDialog.close()
                }

                Controls.AppButton {
                    primary: true
                    text: qsTr("Remove")
                    onClicked: {
                        confirmDeleteDialog.close()
                        if (AppContext.libraryRoots) {
                            AppContext.libraryRoots.removeRoot(root.deleteTargetId)
                        }
                    }
                }
            }
        }
    }

    // Section title
    Label {
        text: qsTr("Library")
        font.pixelSize: Theme.fontSizeLarge
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }

    // Roots list or empty hint
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Repeater {
            id: rootsRepeater
            model: AppContext.libraryRoots

            delegate: Rectangle {
                id: rowRect
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navItemHeight
                color: Theme.surfaceVariant
                radius: Theme.radiusMedium
                border.color: Theme.divider
                border.width: 1

                required property var rootId
                required property string path
                required property bool rootEnabled

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    spacing: Theme.spacingSmall

                    Controls.AppCheckBox {
                        id: rootCheckBox
                        checked: rowRect.rootEnabled
                        onToggled: {
                            if (AppContext.libraryRoots) {
                                AppContext.libraryRoots.setRootEnabled(rowRect.rootId, checked)
                            }
                        }
                    }

                    Label {
                        text: rowRect.path
                        font.pixelSize: Theme.fontSizeNormal
                        color: rowRect.rootEnabled ? Theme.text : Theme.textSecondary
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Controls.IconButton {
                        icon.source: "icons/trash-2.svg"
                        toolTip: qsTr("Remove folder from library")
                        onClicked: {
                            root.deleteTargetId = rowRect.rootId
                            root.deleteTargetPath = rowRect.path
                            confirmDeleteDialog.open()
                        }
                    }
                }
            }
        }

        Label {
            visible: rootsRepeater.count === 0
            text: qsTr("No music folders yet. Add a folder to build your library.")
            font.pixelSize: Theme.fontSizeNormal
            color: Theme.textSecondary
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSmall
            Layout.bottomMargin: Theme.spacingSmall
        }
    }

    Label {
        visible: root.errorMessage.length > 0
        text: root.errorMessage
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.errorText
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    // Action buttons row: "Add Folder..." and "Rescan Now"
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        Controls.AppButton {
            text: qsTr("Add Folder…")
            primary: true
            onClicked: {
                root.errorMessage = ""
                folderDialog.open()
            }
        }

        Controls.AppButton {
            text: AppContext.scanning ? qsTr("Scanning…") : qsTr("Rescan Now")
            enabled: !AppContext.scanning && AppContext.libraryReady
            onClicked: AppContext.rescan()
        }

        Item {
            Layout.fillWidth: true
        }
    }

    // Artist name display preference
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingMedium
        spacing: Theme.spacingTiny

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingMedium

            Label {
                text: qsTr("Artist name display")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.text
                Layout.preferredWidth: 140
            }

            Controls.AppComboBox {
                id: artistNamePrefCombo
                Layout.preferredWidth: 200
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("Original"), value: ArtistNames.Preference.Original },
                    { text: qsTr("Simplified Chinese"), value: ArtistNames.Preference.SimplifiedChinese },
                    { text: qsTr("English"), value: ArtistNames.Preference.English }
                ]
                onActivated: {
                    if (AppContext.settings) {
                        AppContext.settings.artistNamePreference = currentValue
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            text: qsTr("Uses names found by the library butler; falls back to the original name.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    // Automatic cleanup
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingMedium
        spacing: Theme.spacingTiny

        Controls.AppCheckBox {
            id: autoCleanupCheck
            text: qsTr("Clean up new files automatically")
            checked: AppContext.settings ? AppContext.settings.autoCleanup : true
            onToggled: {
                if (AppContext.settings) {
                    AppContext.settings.autoCleanup = checked
                }
            }

            Connections {
                target: AppContext.settings
                function onAutoCleanupChanged() {
                    if (AppContext.settings) {
                        autoCleanupCheck.checked = AppContext.settings.autoCleanup
                    }
                }
            }
        }

        Label {
            text: qsTr("After new files are scanned, fix garbled tags with rules, apply known artist credits and group song versions. AI and online steps still run only when you start them.")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
