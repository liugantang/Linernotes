// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QtGlobal>

#include <core/Result.h>
#include <nlq/NlqQuery.h>

#include <optional>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {
class Recommender;
} // namespace linernotes::rec

namespace linernotes::nlq {

class QueryRunner;

[[nodiscard]] core::Result<qint64> resolveSimilarSeed(
    library::Database &db, const SimilarTo &similarTo, std::optional<qint64> currentTrackId);

[[nodiscard]] core::Result<QList<qint64>> runSimilarQuery(const QueryRunner &runner,
    rec::Recommender &recommender, const Query &query, qint64 seedTrackId);

} // namespace linernotes::nlq
