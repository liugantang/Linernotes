// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import Linernotes
import "controls" as Controls

Popup {
    id: root

    enum Step {
        Welcome,
        Folder,
        Scanning
    }

    property int currentStep: FirstRunWizard.Welcome
    property var selectedFolder: null
    property string errorMessage: ""
    property bool hasStartedScan: false

    modal: true
    focus: true
    dim: true
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay

    implicitWidth: 520
    implicitHeight: 420
    padding: Theme.spacingLarge

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.divider
        border.width: 1
        radius: Theme.cardBorderRadius
    }

    function formatFolderPath(url) {
        if (!url) return ""
        let s = url.toString()
        if (s.startsWith("file://")) {
            s = s.substring(7)
        }
        return decodeURIComponent(s)
    }

    function finish() {
        if (AppContext.settings) {
            AppContext.settings.firstRunCompleted = true
        }
        root.close()
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Choose Music Folder")
        currentFolder: StandardPaths.writableLocation(StandardPaths.MusicLocation)
        onAccepted: {
            root.selectedFolder = selectedFolder
            root.errorMessage = ""
        }
    }

    contentItem: StackLayout {
        currentIndex: root.currentStep

        // Step 1: Welcome
        ColumnLayout {
            spacing: Theme.spacingLarge
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingLarge

            Item {
                Layout.fillHeight: true
            }

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 72
                height: 72
                radius: 36
                color: Theme.surfaceVariant

                Image {
                    anchors.centerIn: parent
                    source: "icons/music.svg"
                    width: 36
                    height: 36
                }
            }

            Label {
                text: AppInfo.name
                font.pixelSize: Theme.fontSizeTitle
                font.bold: true
                color: Theme.accent
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("Linernotes plays your local music library.")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Item {
                Layout.fillHeight: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Get Started")
                    primary: true
                    onClicked: root.currentStep = FirstRunWizard.Folder
                }
            }
        }

        // Step 2: Folder
        ColumnLayout {
            spacing: Theme.spacingMedium
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingLarge

            Label {
                text: qsTr("Choose Music Folder")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Select a folder where your music files are stored.")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Item {
                Layout.preferredHeight: Theme.spacingSmall
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                radius: Theme.radiusMedium
                color: Theme.surfaceVariant
                border.color: Theme.divider
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    spacing: Theme.spacingMedium

                    Image {
                        source: "icons/library.svg"
                        width: Theme.iconSizeSmall
                        height: Theme.iconSizeSmall
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Label {
                        text: root.selectedFolder ? root.formatFolderPath(root.selectedFolder) : qsTr("No folder chosen")
                        font.pixelSize: Theme.fontSizeNormal
                        font.bold: root.selectedFolder !== null && root.selectedFolder !== undefined && root.selectedFolder.toString().length > 0
                        color: root.selectedFolder ? Theme.text : Theme.textSecondary
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Controls.AppButton {
                        text: root.selectedFolder ? qsTr("Change…") : qsTr("Choose Folder…")
                        primary: !root.selectedFolder
                        onClicked: {
                            root.errorMessage = ""
                            folderDialog.open()
                        }
                    }
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

            Item {
                Layout.fillHeight: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Controls.AppButton {
                    text: qsTr("Skip")
                    onClicked: root.finish()
                }

                Item {
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    text: qsTr("Continue")
                    primary: true
                    enabled: root.selectedFolder !== null && root.selectedFolder !== undefined && root.selectedFolder.toString().length > 0
                    onClicked: {
                        root.errorMessage = ""
                        if (AppContext.libraryRoots) {
                            const ok = AppContext.libraryRoots.addRoot(root.selectedFolder)
                            if (ok) {
                                root.hasStartedScan = true
                                root.currentStep = FirstRunWizard.Scanning
                            } else {
                                root.errorMessage = qsTr("Failed to add folder. The folder may not exist or is already in the library.")
                            }
                        }
                    }
                }
            }
        }

        // Step 3: Scanning
        ColumnLayout {
            spacing: Theme.spacingMedium
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingLarge

            Item {
                Layout.fillHeight: true
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                running: AppContext.scanning
                visible: AppContext.scanning
            }

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 48
                height: 48
                radius: 24
                color: Theme.surfaceVariant
                visible: !AppContext.scanning

                Image {
                    anchors.centerIn: parent
                    source: "icons/music.svg"
                    width: 24
                    height: 24
                }
            }

            Label {
                text: AppContext.scanning ? qsTr("Scanning your library…") : qsTr("Done")
                font.pixelSize: Theme.fontSizeLarge
                font.bold: true
                color: Theme.text
                Layout.alignment: Qt.AlignHCenter
            }

            ProgressBar {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 300
                indeterminate: true
                visible: AppContext.scanning
            }

            Label {
                text: AppContext.scanning ? qsTr("This might take a moment depending on the library size.") : qsTr("Your library is ready to play.")
                font.pixelSize: Theme.fontSizeNormal
                color: Theme.textSecondary
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Item {
                Layout.fillHeight: true
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Controls.AppButton {
                    visible: AppContext.scanning
                    text: qsTr("Continue in Background")
                    onClicked: root.finish()
                }

                Controls.AppButton {
                    visible: !AppContext.scanning
                    primary: true
                    text: qsTr("Start Listening")
                    onClicked: root.finish()
                }
            }
        }
    }
}
