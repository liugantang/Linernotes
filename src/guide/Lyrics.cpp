// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Lyrics.h"

#include <QRegularExpression>
#include <QStringTokenizer>

#include <algorithm>

namespace linernotes::guide {

bool looksSynced(const QString &text)
{
    static const QRegularExpression s_syncedPattern(QStringLiteral(R"(^\s*\[\d+:\d+)"));
    const auto tokenizer = QStringTokenizer { text, QLatin1Char('\n') };
    return std::ranges::any_of(
        tokenizer, [](const auto line) { return s_syncedPattern.matchView(line).hasMatch(); });
}

} // namespace linernotes::guide
