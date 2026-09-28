// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace linernotes::butler {

enum class SplitVerdict : std::uint8_t { Keep, Split, Ambiguous };

struct SplitDecision {
    SplitVerdict verdict = SplitVerdict::Keep;
    QStringList parts; // Split 时为拆分结果（去空白、去重、保持顺序）；Ambiguous 时为规则的建议拆法
    double confidence = 0.0; // Split 时有意义
    QString reason { };

    bool operator==(const SplitDecision &) const = default;
};

/// 是否包含顶层艺人分隔符（强/弱分隔符，括号内不算）。
bool hasSeparators(const QString &value);

/// knownArtists：曲库中作为独立值出现过的艺人名（大小写不敏感比较）。
SplitDecision decideSplit(const QString &value, const QSet<QString> &knownArtists);

} // namespace linernotes::butler
