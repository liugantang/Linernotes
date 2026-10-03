// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Clock.h>
#include <core/Result.h>
#include <guide/Knowledge.h>
#include <library/Database.h>

#include <cstdint>

namespace linernotes::guide {

enum class LookupState : std::uint8_t { Missing, NotFound, Found };

struct CachedLookup {
    LookupState state = LookupState::Missing;
    KnowledgeEntry entry { };
    bool operator==(const CachedLookup &) const = default;
};

class KnowledgeStore {
public:
    explicit KnowledgeStore(library::Database &db, const core::Clock &clock);

    core::Result<CachedLookup> get(const QString &key) const;
    core::Result<void> saveFound(const QString &key, const KnowledgeEntry &entry) const;
    core::Result<void> saveNotFound(const QString &key) const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::guide
