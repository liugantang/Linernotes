// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SimilarTracks.h"

#include <QList>
#include <QSet>
#include <QSqlQuery>
#include <QString>

#include <audio/EmbeddingIndex.h>
#include <library/Database.h>
#include <rec/SoundIndex.h>

#include <algorithm>

namespace linernotes::rec {

SimilarTracks::SimilarTracks(library::Database &db, SoundIndex &soundIndex)
    : m_db(db)
    , m_soundIndex(soundIndex)
{
}

bool SimilarTracks::hasEmbedding(qint64 trackId)
{
    if (trackId <= 0) {
        return false;
    }
    const auto indexRes = m_soundIndex.index();
    if (!indexRes.ok() || indexRes.value() == nullptr) {
        return false;
    }
    return indexRes.value()->contains(trackId);
}

core::Result<QList<audio::Neighbor>> SimilarTracks::similarTo(qint64 trackId, int k)
{
    if (k <= 0 || trackId <= 0) {
        return QList<audio::Neighbor> { };
    }

    const auto indexRes = m_soundIndex.index();
    if (!indexRes.ok()) {
        return indexRes.error();
    }

    const auto *index = indexRes.value();
    if (index == nullptr || !index->contains(trackId)) {
        return QList<audio::Neighbor> { };
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSet<qint64> sameWorkIds;
    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral(
        "SELECT id FROM tracks WHERE work_id = (SELECT work_id FROM tracks WHERE id = ?) "
        "AND work_id IS NOT NULL;"));
    q.addBindValue(trackId);
    if (q.exec()) {
        while (q.next()) {
            sameWorkIds.insert(q.value(0).toLongLong());
        }
    }

    const int fetchK = (k * 2) + 20;
    const auto rawNeighbors = index->nearest(trackId, fetchK);

    QList<audio::Neighbor> filtered;
    filtered.reserve(std::min(static_cast<qsizetype>(k), rawNeighbors.size()));
    for (const auto &n : rawNeighbors) {
        if (n.score > 0.98F) {
            continue;
        }
        if (sameWorkIds.contains(n.id)) {
            continue;
        }
        filtered.append(n);
        if (filtered.size() >= static_cast<qsizetype>(k)) {
            break;
        }
    }

    return filtered;
}

} // namespace linernotes::rec
