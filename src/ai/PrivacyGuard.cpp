// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PrivacyGuard.h"

#include <QHostAddress>

#include <core/Settings.h>

#include <algorithm>

namespace linernotes::ai {

namespace {

inline const core::SettingKey<bool> kPlayHistoryKey { u"privacy/play_history", true };
inline const core::SettingKey<bool> kMomentsKey { u"privacy/moments", false };
inline const core::SettingKey<bool> kLocationKey { u"privacy/location", false };

const core::SettingKey<bool> &settingKeyFor(DataCategory category)
{
    switch (category) {
    case DataCategory::PlayHistory:
        return kPlayHistoryKey;
    case DataCategory::Moments:
        return kMomentsKey;
    case DataCategory::Location:
        return kLocationKey;
    }
    return kLocationKey;
}

} // namespace

PrivacyGuard::PrivacyGuard(core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

bool PrivacyGuard::isAllowed(DataCategory category) const
{
    return m_settings.value(settingKeyFor(category));
}

void PrivacyGuard::setAllowed(DataCategory category, bool allowed)
{
    const auto &key = settingKeyFor(category);
    if (m_settings.value(key) == allowed) {
        return;
    }
    m_settings.setValue(key, allowed);
    emit changed();
}

bool PrivacyGuard::isLocalService(const QUrl &baseUrl)
{
    QString host = baseUrl.host();
    if (host.isEmpty()) {
        return false;
    }
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
        host = host.mid(1, host.size() - 2);
    }
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    QHostAddress ip;
    if (ip.setAddress(host)) {
        if (ip.isLoopback() || ip.isPrivateUse() || ip.isLinkLocal()) {
            return true;
        }
    }
    return false;
}

bool PrivacyGuard::isAllowedFor(DataCategory category, const QUrl &baseUrl) const
{
    if (isLocalService(baseUrl)) {
        return true;
    }
    return isAllowed(category);
}

QList<DataCategory> PrivacyGuard::blocked(
    const QSet<DataCategory> &categories, const QUrl &baseUrl) const
{
    if (isLocalService(baseUrl) || categories.isEmpty()) {
        return { };
    }
    QList<DataCategory> result;
    for (const auto cat : categories) {
        if (!isAllowed(cat)) {
            result.append(cat);
        }
    }
    std::ranges::sort(result);
    return result;
}

} // namespace linernotes::ai
