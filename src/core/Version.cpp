// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <core/Version.h>

namespace linernotes::core {

QString versionString()
{
    return QStringLiteral("%1.%2.%3").arg(kVersionMajor).arg(kVersionMinor).arg(kVersionPatch);
}

QString applicationName()
{
    return QStringLiteral("Linernotes");
}

} // namespace linernotes::core
