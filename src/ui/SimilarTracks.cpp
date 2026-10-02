// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SimilarTracks.h"

#include <QList>
#include <QSet>
#include <QSqlQuery>
#include <QString>

#include <audio/EmbeddingIndex.h>
#include <audio/TrackEmbedding.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace linernotes::ui {

SimilarTracks::SimilarTracks(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<void> SimilarTracks::ensureIndexLoaded()
{
    const library::EmbeddingStore store(m_db, m_clock);
    const QString model = QString(audio::kEmbeddingModelId);
    const auto countRes = store.analyzedCount(model);
    if (!countRes.ok()) {
        return countRes.error();
    }

    const int currentCount = countRes.value();
    if (m_index != nullptr && m_indexedCount == currentCount) {
        return { };
    }

    const auto loadRes = store.loadAll(model);
    if (!loadRes.ok()) {
        return loadRes.error();
    }

    const auto &embeddings = loadRes.value();
    const int dim
        = embeddings.isEmpty() ? 1024 : static_cast<int>(embeddings.first().vector.size());
    auto newIndex = std::make_unique<audio::EmbeddingIndex>(dim);
    for (const auto &se : embeddings) {
        newIndex->add(se.trackId, se.vector);
    }

    m_index = std::move(newIndex);
    m_indexedCount = currentCount;
    return { };
}

bool SimilarTracks::hasEmbedding(qint64 trackId)
{
    if (trackId <= 0) {
        return false;
    }
    const auto loadRes = ensureIndexLoaded();
    if (!loadRes.ok() || m_index == nullptr) {
        return false;
    }
    return m_index->contains(trackId);
}

core::Result<QList<audio::Neighbor>> SimilarTracks::similarTo(qint64 trackId, int k)
{
    if (k <= 0 || trackId <= 0) {
        return QList<audio::Neighbor> { };
    }

    const auto loadRes = ensureIndexLoaded();
    if (!loadRes.ok()) {
        return loadRes.error();
    }

    if (m_index == nullptr || !m_index->contains(trackId)) {
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
    const auto rawNeighbors = m_index->nearest(trackId, fetchK);

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

} // namespace linernotes::ui
