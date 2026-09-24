// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

#include "AppInfo.h"

#include <core/Version.h>

AppInfo::AppInfo(QObject *parent)
    : QObject(parent)
{
}

QString AppInfo::name() const
{
    return aimusic::core::applicationName();
}

QString AppInfo::version() const
{
    return aimusic::core::versionString();
}
