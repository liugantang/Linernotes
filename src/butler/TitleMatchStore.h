// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QPair>
#include <QString>

#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

inline constexpr double kTitleMatchMinConfidence = 0.8;

struct TitleMatchVerdict {
    bool same = false;
    double confidence = 0.0;
    QString reason { };
    bool operator==(const TitleMatchVerdict &) const = default;
};

class TitleMatchStore {
public:
    TitleMatchStore(library::Database &db, const core::Clock &clock);

    /// (keyA, keyB) → 结论；只返回 prompt_version 相同的行
    core::Result<QHash<QPair<QString, QString>, TitleMatchVerdict>> loadAll(
        int promptVersion) const;

    /// INSERT OR REPLACE，一个事务
    core::Result<void> save(const QHash<QPair<QString, QString>, TitleMatchVerdict> &verdicts,
        const QString &model, int promptVersion) const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
