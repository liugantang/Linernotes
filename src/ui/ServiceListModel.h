// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include <ai/AiConfig.h>

#include <cstdint>

namespace linernotes::ui {

class ServiceListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ServiceListModel)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt model roles need
                                // int conversion
        ServiceIdRole = Qt::UserRole + 1,
        NameRole,
        BaseUrlRole,
        DefaultModelRole,
        TimeoutMsRole,
        MaxConcurrentRole,
        RequestsPerMinuteRole,
        IsDefaultRole,
        IsLocalRole,
        CapabilitiesKnownRole,
        SupportsJsonSchemaRole,
        SupportsToolsRole,
        SupportsJsonObjectRole,
    };
    Q_ENUM(Role)

    explicit ServiceListModel(ai::AiConfig &config, QObject *parent = nullptr);
    ~ServiceListModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    ai::AiConfig &m_config;
    QList<ai::ServiceProfile> m_services;
    QString m_defaultServiceId;
};

} // namespace linernotes::ui
