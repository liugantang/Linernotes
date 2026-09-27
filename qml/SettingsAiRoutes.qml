// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Linernotes
import "controls" as Controls

ColumnLayout {
    id: root
    spacing: Theme.spacingSmall

    // rowCount() 不是可绑定属性；ServiceListModel 只做整体重置，在 modelReset 时刷新
    property bool hasServices: false

    function updateHasServices() {
        hasServices = !!AppContext.aiSettings && !!AppContext.aiSettings.services
            && AppContext.aiSettings.services.rowCount() > 0
    }

    Component.onCompleted: updateHasServices()

    Connections {
        target: AppContext.aiSettings ? AppContext.aiSettings.services : null
        function onModelReset() { root.updateHasServices() }
    }

    function getFeatures() {
        return [
            { purpose: Ai.Cleanup, name: qsTr("Metadata cleanup") },
            { purpose: Ai.Query, name: qsTr("Natural-language search") },
            { purpose: Ai.Dj, name: qsTr("AI DJ") },
            { purpose: Ai.Guide, name: qsTr("Listening guide") },
            { purpose: Ai.Narrative, name: qsTr("Liner notes & narrative") }
        ]
    }

    // Subtitle
    Label {
        text: qsTr("Models by Feature")
        font.pixelSize: Theme.fontSizeNormal
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }

    Label {
        visible: !root.hasServices
        text: qsTr("Add a service above to configure feature routing.")
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall
        enabled: root.hasServices
        opacity: root.hasServices ? 1.0 : 0.5

        Repeater {
            id: routesRepeater
            model: root.getFeatures()

            delegate: RowLayout {
                id: routeRow
                Layout.fillWidth: true
                spacing: Theme.spacingMedium

                required property var modelData
                required property int index

                Label {
                    text: routeRow.modelData.name
                    font.pixelSize: Theme.fontSizeNormal
                    color: Theme.text
                    Layout.preferredWidth: 170
                    elide: Text.ElideRight
                }

                Controls.AppComboBox {
                    id: serviceCombo
                    Layout.preferredWidth: 200
                    textRole: "text"
                    valueRole: "value"
                    enabled: root.hasServices

                    function rebuildModel() {
                        const list = [{ text: qsTr("Default service"), value: "", defaultModel: "" }]
                        if (AppContext.aiSettings && AppContext.aiSettings.services) {
                            const sm = AppContext.aiSettings.services
                            let defSvcDefaultModel = ""
                            for (let i = 0; i < sm.rowCount(); ++i) {
                                const idx = sm.index(i, 0)
                                const sId = sm.data(idx, ServiceListModel.ServiceIdRole)
                                const sName = sm.data(idx, ServiceListModel.NameRole)
                                const dModel = sm.data(idx, ServiceListModel.DefaultModelRole)
                                const isDef = sm.data(idx, ServiceListModel.IsDefaultRole)
                                if (isDef) {
                                    defSvcDefaultModel = dModel
                                }
                                list.push({ text: sName, value: sId, defaultModel: dModel })
                            }
                            list[0].defaultModel = defSvcDefaultModel
                        }
                        serviceCombo.model = list
                        syncSelection()
                    }

                    function syncSelection() {
                        if (!AppContext.aiSettings) return
                        const currentSvc = AppContext.aiSettings.routeServiceId(routeRow.modelData.purpose)
                        const idx = indexOfValue(currentSvc)
                        currentIndex = idx >= 0 ? idx : 0
                    }

                    onActivated: {
                        if (AppContext.aiSettings) {
                            AppContext.aiSettings.setRoute(
                                routeRow.modelData.purpose, currentValue, modelField.text.trim())
                        }
                    }
                }

                Controls.AppTextField {
                    id: modelField
                    Layout.fillWidth: true
                    enabled: root.hasServices
                    placeholderText: {
                        if (serviceCombo.currentIndex >= 0 && serviceCombo.model
                            && serviceCombo.model[serviceCombo.currentIndex]) {
                            const dModel = serviceCombo.model[serviceCombo.currentIndex].defaultModel
                            if (dModel && dModel.length > 0) {
                                return dModel
                            }
                        }
                        return qsTr("Default model")
                    }

                    function syncText() {
                        if (!AppContext.aiSettings) return
                        text = AppContext.aiSettings.routeModel(routeRow.modelData.purpose)
                    }

                    onEditingFinished: {
                        if (AppContext.aiSettings) {
                            AppContext.aiSettings.setRoute(
                                routeRow.modelData.purpose, serviceCombo.currentValue, text.trim())
                        }
                    }
                }

                Component.onCompleted: {
                    serviceCombo.rebuildModel()
                    modelField.syncText()
                }

                Connections {
                    target: AppContext.aiSettings
                    function onRoutesChanged() {
                        serviceCombo.syncSelection()
                        modelField.syncText()
                    }
                }

                Connections {
                    target: AppContext.aiSettings ? AppContext.aiSettings.services : null
                    function onModelReset() {
                        serviceCombo.rebuildModel()
                    }
                    function onRowsInserted() {
                        serviceCombo.rebuildModel()
                    }
                    function onRowsRemoved() {
                        serviceCombo.rebuildModel()
                    }
                    function onDataChanged() {
                        serviceCombo.rebuildModel()
                    }
                }
            }
        }
    }
}
