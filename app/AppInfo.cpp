// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AppInfo.h"

#include <core/Version.h>

AppInfo::AppInfo(QObject *parent)
    : QObject(parent)
{
}

QString AppInfo::name() const
{
    return linernotes::core::applicationName();
}

QString AppInfo::version() const
{
    return linernotes::core::versionString();
}
