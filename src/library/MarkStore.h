// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>

namespace linernotes::library {

class Database;

class MarkStore {
public:
    explicit MarkStore(Database &db);

    core::Result<void> setFavorite(FavoriteKind kind, const QList<qint64> &ids, bool favorite);
    core::Result<bool> isFavorite(FavoriteKind kind, qint64 id) const;
    core::Result<void> setRating(const QList<qint64> &trackIds, int rating);
    core::Result<int> rating(qint64 trackId) const;

private:
    Database &m_db;
};

} // namespace linernotes::library
