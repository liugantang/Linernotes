// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QString>

#include <butler/ArtistCredit.h>
#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct StoredArtistCredit {
    ArtistCredit credit;
    QString model { };
    int promptVersion = 0;
    qint64 parsedAt = 0;
    bool operator==(const StoredArtistCredit &) const = default;
};

class ArtistCreditStore {
public:
    ArtistCreditStore(library::Database &db, const core::Clock &clock);

    core::Result<std::optional<StoredArtistCredit>> load(const QString &value) const;
    core::Result<QHash<QString, StoredArtistCredit>> loadAll() const;
    /// 同一事务内 INSERT OR REPLACE。
    core::Result<void> save(
        const QHash<QString, ArtistCredit> &credits, const QString &model, int promptVersion);

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
