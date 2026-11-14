// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1Char>
#include <QString>
#include <QStringLiteral>

namespace linernotes::guide {

// QUrlQuery 不编码 '+'，服务器会把它当作空格；'%' 也要先转义，否则会被当成已编码的序列
inline QString queryValue(QString value)
{
    return value.replace(QLatin1Char('%'), QStringLiteral("%25"))
        .replace(QLatin1Char('+'), QStringLiteral("%2B"));
}

} // namespace linernotes::guide
