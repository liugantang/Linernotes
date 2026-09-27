// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ServiceListModel.h"

#include <ai/PrivacyGuard.h>

namespace linernotes::ui {

ServiceListModel::ServiceListModel(ai::AiConfig &config, QObject *parent)
    : QAbstractListModel(parent)
    , m_config(config)
{
    connect(&m_config, &ai::AiConfig::changed, this, &ServiceListModel::refresh);
    refresh();
}

int ServiceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_services.size());
}

QVariant ServiceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_services.size()) {
        return { };
    }

    const auto &profile = m_services.at(index.row());
    switch (role) {
    case ServiceIdRole:
        return profile.id;
    case NameRole:
        return profile.name;
    case BaseUrlRole:
        return profile.baseUrl.toString();
    case DefaultModelRole:
        return profile.defaultModel;
    case TimeoutMsRole:
        return profile.timeoutMs;
    case MaxConcurrentRole:
        return profile.maxConcurrent;
    case RequestsPerMinuteRole:
        return profile.requestsPerMinute;
    case IsDefaultRole:
        return profile.id == m_defaultServiceId;
    case IsLocalRole:
        return ai::PrivacyGuard::isLocalService(profile.baseUrl);
    case CapabilitiesKnownRole:
        return profile.capabilities.has_value();
    case SupportsJsonSchemaRole:
        return profile.capabilities.has_value() && profile.capabilities->jsonSchema;
    case SupportsToolsRole:
        return profile.capabilities.has_value() && profile.capabilities->tools;
    case SupportsJsonObjectRole:
        return profile.capabilities.has_value() && profile.capabilities->jsonObject;
    default:
        return { };
    }
}

QHash<int, QByteArray> ServiceListModel::roleNames() const
{
    return {
        { ServiceIdRole, "serviceId" },
        { NameRole, "name" },
        { BaseUrlRole, "baseUrl" },
        { DefaultModelRole, "defaultModel" },
        { TimeoutMsRole, "timeoutMs" },
        { MaxConcurrentRole, "maxConcurrent" },
        { RequestsPerMinuteRole, "requestsPerMinute" },
        { IsDefaultRole, "isDefault" },
        { IsLocalRole, "isLocal" },
        { CapabilitiesKnownRole, "capabilitiesKnown" },
        { SupportsJsonSchemaRole, "supportsJsonSchema" },
        { SupportsToolsRole, "supportsTools" },
        { SupportsJsonObjectRole, "supportsJsonObject" },
    };
}

void ServiceListModel::refresh()
{
    beginResetModel();
    m_services = m_config.services();
    m_defaultServiceId = m_config.defaultServiceId();
    endResetModel();
}

} // namespace linernotes::ui
