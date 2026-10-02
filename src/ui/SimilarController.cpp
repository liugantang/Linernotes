// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SimilarController.h"

#include <QVariantMap>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <rec/SimilarTracks.h>
#include <ui/Format.h>
#include <ui/LibraryActions.h>

namespace linernotes::ui {

namespace {

QVariantList populateTrackRows(const QList<library::TrackRow> &tracks)
{
    QVariantList rows;
    rows.reserve(tracks.size());
    for (const auto &t : tracks) {
        QVariantMap map;
        map.insert(QStringLiteral("trackId"), t.trackId);
        map.insert(QStringLiteral("title"), t.title);
        map.insert(QStringLiteral("artist"), t.artist);
        map.insert(QStringLiteral("album"), t.album);
        map.insert(QStringLiteral("albumId"), t.albumId.value_or(0));
        map.insert(QStringLiteral("durationText"), formatDuration(t.durationMs));
        map.insert(QStringLiteral("coverHash"), t.coverHash);
        rows.append(map);
    }
    return rows;
}

} // namespace

SimilarController::SimilarController(rec::SimilarTracks &similarTracks, library::Database &db,
    LibraryActions &actions, QObject *parent)
    : QObject(parent)
    , m_similarTracks(similarTracks)
    , m_db(db)
    , m_actions(actions)
{
}

QString SimilarController::seedTitle() const
{
    return m_seedTitle;
}

QVariantList SimilarController::rows() const
{
    return m_rows;
}

bool SimilarController::isSeedAnalyzed() const
{
    return m_seedAnalyzed;
}

void SimilarController::find(qint64 trackId)
{
    m_seedTrackId = trackId;
    m_seedTitle.clear();
    m_rows.clear();
    m_seedAnalyzed = false;

    if (trackId <= 0) {
        emit resultsChanged();
        emit openRequested();
        return;
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        emit resultsChanged();
        emit openRequested();
        return;
    }

    const library::LibraryQuery lq(connRes.value());
    const auto seedTrackRes = lq.tracksByIds({ trackId });
    if (seedTrackRes.ok() && !seedTrackRes.value().isEmpty()) {
        const auto &st = seedTrackRes.value().first();
        const QString title = st.title.isEmpty() ? tr("Unknown Title") : st.title;
        m_seedTitle = st.artist.isEmpty() ? title : QStringLiteral("%1 - %2").arg(title, st.artist);
    }

    m_seedAnalyzed = m_similarTracks.hasEmbedding(trackId);
    if (!m_seedAnalyzed) {
        emit resultsChanged();
        emit openRequested();
        return;
    }

    constexpr int kSimilarCount = 50;
    const auto similarRes = m_similarTracks.similarTo(trackId, kSimilarCount);
    if (similarRes.ok()) {
        const auto &neighbors = similarRes.value();
        QList<qint64> resultIds;
        resultIds.reserve(neighbors.size());
        for (const auto &n : neighbors) {
            resultIds.append(n.id);
        }

        if (!resultIds.isEmpty()) {
            const auto tracksRes = lq.tracksByIds(resultIds);
            if (tracksRes.ok()) {
                m_rows = populateTrackRows(tracksRes.value());
            }
        }
    }

    emit resultsChanged();
    emit openRequested();
}

QList<qint64> SimilarController::collectResultTrackIds() const
{
    QList<qint64> trackIds;
    trackIds.reserve(m_rows.size());
    for (const auto &rowVar : m_rows) {
        const auto map = rowVar.toMap();
        trackIds.append(map.value(QStringLiteral("trackId")).toLongLong());
    }
    return trackIds;
}

void SimilarController::playAll()
{
    QList<qint64> trackIds;
    trackIds.reserve(1 + m_rows.size());
    if (m_seedTrackId > 0) {
        trackIds.append(m_seedTrackId);
    }
    for (const auto &rowVar : m_rows) {
        const auto map = rowVar.toMap();
        trackIds.append(map.value(QStringLiteral("trackId")).toLongLong());
    }

    if (!trackIds.isEmpty()) {
        m_actions.playTracks(trackIds, 0, core::PlaySource::Library);
    }
}

void SimilarController::playRow(int index)
{
    if (index < 0 || index >= m_rows.size()) {
        return;
    }

    const QList<qint64> trackIds = collectResultTrackIds();
    if (index < trackIds.size()) {
        m_actions.playTracks(trackIds, index, core::PlaySource::Library);
    }
}

void SimilarController::enqueueAll()
{
    const QList<qint64> trackIds = collectResultTrackIds();
    if (!trackIds.isEmpty()) {
        m_actions.enqueue(trackIds);
    }
}

} // namespace linernotes::ui
