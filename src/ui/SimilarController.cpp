// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SimilarController.h"

#include <QVariantMap>

#include <core/PlaySource.h>
#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <rec/Recommender.h>
#include <rec/SimilarTracks.h>
#include <ui/Format.h>
#include <ui/LibraryActions.h>
#include <ui/PlaylistController.h>

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

SimilarController::SimilarController(rec::SimilarTracks &similarTracks,
    rec::Recommender &recommender, PlaylistController &playlists, library::Database &db,
    LibraryActions &actions, QObject *parent)
    : QObject(parent)
    , m_similarTracks(similarTracks)
    , m_recommender(recommender)
    , m_playlists(playlists)
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

bool SimilarController::isPlaylistMode() const
{
    return m_playlistMode;
}

void SimilarController::find(qint64 trackId)
{
    m_playlistMode = false;
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

void SimilarController::playlistFromTrack(qint64 trackId)
{
    m_playlistMode = true;
    m_seedTrackId = trackId;
    m_seedTitle.clear();
    m_rows.clear();
    m_seedAnalyzed = true;

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
    if (!seedTrackRes.ok() || seedTrackRes.value().isEmpty()) {
        emit resultsChanged();
        emit openRequested();
        return;
    }

    const auto &st = seedTrackRes.value().first();
    const QString title = st.title.isEmpty() ? tr("Unknown Title") : st.title;
    m_seedTitle = st.artist.isEmpty() ? title : QStringLiteral("%1 - %2").arg(title, st.artist);

    rec::RecRequest req;
    req.seeds = { rec::Seed { .trackId = trackId, .weight = 1.0 } };
    req.count = 30;
    req.recentExcludeMs = 0;
    req.freshnessWeight = 0.2;
    req.randomness = 0.0;

    m_rows = populateTrackRows(seedTrackRes.value());

    const auto recRes = m_recommender.recommend(req);
    if (recRes.ok() && !recRes.value().isEmpty()) {
        const auto tracksRes = lq.tracksByIds(recRes.value());
        if (tracksRes.ok()) {
            m_rows.append(populateTrackRows(tracksRes.value()));
        }
    }

    emit resultsChanged();
    emit openRequested();
}

void SimilarController::playlistFromAlbum(qint64 albumId)
{
    m_playlistMode = true;
    m_seedTrackId = 0;
    m_seedTitle.clear();
    m_rows.clear();
    m_seedAnalyzed = true;

    if (albumId <= 0) {
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
    const auto albumRes = lq.album(albumId);
    if (albumRes.ok() && albumRes.value().has_value()) {
        const auto &ab = albumRes.value().value();
        const QString title = ab.title.isEmpty() ? tr("Unknown Album") : ab.title;
        m_seedTitle = ab.albumArtist.isEmpty()
            ? title
            : QStringLiteral("%1 - %2").arg(title, ab.albumArtist);
    }

    library::TrackFilter filter;
    filter.albumId = albumId;
    const auto trackIdsRes
        = lq.trackIds(filter, library::TrackSortKey::Default, Qt::AscendingOrder);
    if (!trackIdsRes.ok() || trackIdsRes.value().isEmpty()) {
        emit resultsChanged();
        emit openRequested();
        return;
    }

    rec::RecRequest req;
    req.count = 30;
    req.recentExcludeMs = 0;
    req.freshnessWeight = 0.2;
    req.randomness = 0.0;
    req.seeds.reserve(trackIdsRes.value().size());
    for (const qint64 tid : trackIdsRes.value()) {
        req.seeds.append(rec::Seed { .trackId = tid, .weight = 1.0 });
    }

    const auto recRes = m_recommender.recommend(req);
    if (recRes.ok() && !recRes.value().isEmpty()) {
        const auto tracksRes = lq.tracksByIds(recRes.value());
        if (tracksRes.ok()) {
            m_rows = populateTrackRows(tracksRes.value());
        }
    }

    emit resultsChanged();
    emit openRequested();
}

qint64 SimilarController::saveAsPlaylist(const QString &name)
{
    const QList<qint64> trackIds = collectResultTrackIds();
    if (trackIds.isEmpty()) {
        return -1;
    }
    const qint64 id = m_playlists.createManual(name, trackIds);
    return id > 0 ? id : -1;
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
    if (m_playlistMode) {
        const QList<qint64> trackIds = collectResultTrackIds();
        if (!trackIds.isEmpty()) {
            m_actions.playTracks(trackIds, 0, core::PlaySource::Playlist);
        }
        return;
    }

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
        const auto source = m_playlistMode ? core::PlaySource::Playlist : core::PlaySource::Library;
        m_actions.playTracks(trackIds, index, source);
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
