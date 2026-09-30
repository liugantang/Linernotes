// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QString>

#include <butler/TitleVersion.h>
#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct SuffixVerdict {
    SuffixClass cls;
    double confidence = 1.0;
    QString reason { };
    bool operator==(const SuffixVerdict &) const = default;
};

class VersionSuffixStore {
public:
    VersionSuffixStore(library::Database &db, const core::Clock &clock);

    /// 键 → 结论；只返回 prompt_version == promptVersion 的行
    core::Result<QHash<QString, SuffixVerdict>> loadAll(int promptVersion) const;
    /// INSERT OR REPLACE，一个事务
    core::Result<void> save(const QHash<QString, SuffixVerdict> &verdicts, const QString &model,
        int promptVersion) const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
