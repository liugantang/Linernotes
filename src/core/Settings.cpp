// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#include "Settings.h"

#include <QSettings>

namespace aimusic::core {

Settings::Settings(const QString &iniFilePath, QObject *parent)
    : QObject(parent)
    , m_settings(std::make_unique<QSettings>(iniFilePath, QSettings::IniFormat))
{
}

Settings::~Settings() = default;

void Settings::sync()
{
    m_settings->sync();
}

bool Settings::containsKey(const QString &keyName) const
{
    return m_settings->contains(keyName);
}

QVariant Settings::rawValue(const QString &keyName) const
{
    return m_settings->value(keyName);
}

void Settings::setRawValue(const QString &keyName, const QVariant &value)
{
    m_settings->setValue(keyName, value);
}

void Settings::removeKey(const QString &keyName)
{
    m_settings->remove(keyName);
}

} // namespace aimusic::core
