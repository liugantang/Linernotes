// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

class TrackIssueStore {
public:
    TrackIssueStore(Database &db, const core::Clock &clock);

    core::Result<void> add(
        qint64 trackId, TrackIssueKind kind, std::optional<TagField> field, const QString &detail);
    core::Result<int> count(TrackIssueKind kind) const;

private:
    Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::library
