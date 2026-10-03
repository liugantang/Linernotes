// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "KnowledgePrewarmer.h"

#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

#include <guide/GuideLogging.h>
#include <guide/GuideSettings.h>
#include <library/LibraryQuery.h>

namespace linernotes::guide {

KnowledgePrewarmer::KnowledgePrewarmer(library::Database &db, KnowledgeService &knowledgeService,
    const core::Clock &clock, core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_knowledgeService(knowledgeService)
    , m_clock(clock)
    , m_settings(settings)
{
    connect(&m_knowledgeService, &KnowledgeService::finished, this,
        &KnowledgePrewarmer::onKnowledgeFinished);
}

KnowledgePrewarmer::~KnowledgePrewarmer()
{
    stop();
}

void KnowledgePrewarmer::start()
{
    if (m_running) {
        return;
    }

    m_running = true;
    emit runningChanged();

    m_pendingTracks.clear();
    m_currentTrackIndex = 0;
    m_consecutiveFailures = 0;
    m_pendingKey.clear();

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        finishRound();
        return;
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT s.track_id "
                             "FROM track_play_stats s "
                             "JOIN tracks t ON s.track_id = t.id "
                             "JOIN files f ON t.file_id = f.id "
                             "WHERE f.missing_since IS NULL AND s.play_count > 0 "
                             "ORDER BY s.play_count DESC, s.last_played_at DESC, s.track_id ASC "
                             "LIMIT 50;"));
    if (!q.exec()) {
        finishRound();
        return;
    }

    QList<qint64> trackIds;
    while (q.next()) {
        trackIds.append(q.value(0).toLongLong());
    }

    if (trackIds.isEmpty()) {
        finishRound();
        return;
    }

    const library::LibraryQuery query(conn);
    const auto tracksRes = query.tracksByIds(trackIds);
    if (!tracksRes.ok() || tracksRes.value().isEmpty()) {
        finishRound();
        return;
    }

    for (const auto &row : tracksRes.value()) {
        if (primaryArtist(row.artist).isEmpty()) {
            continue;
        }
        m_pendingTracks.append(PrewarmTrack {
            .trackId = row.trackId,
            .artist = row.artist,
            .title = row.title,
            .album = row.album,
        });
    }

    if (m_pendingTracks.isEmpty()) {
        finishRound();
        return;
    }

    processNextTrack();
}

void KnowledgePrewarmer::stop()
{
    m_pendingTracks.clear();
    m_pendingKey.clear();
    m_currentTrackIndex = 0;
    m_consecutiveFailures = 0;

    if (m_running) {
        m_running = false;
        emit runningChanged();
    }
}

bool KnowledgePrewarmer::isRunning() const
{
    return m_running;
}

void KnowledgePrewarmer::maybeStart()
{
    if (m_running) {
        return;
    }

    constexpr qint64 kSevenDaysMs = 7LL * 24 * 60 * 60 * 1000;
    const qint64 lastPrewarmAt = m_settings.value(kGuideLastPrewarmAt);
    const qint64 now = m_clock.nowMs();

    if (lastPrewarmAt > 0 && now >= lastPrewarmAt && (now - lastPrewarmAt < kSevenDaysMs)) {
        return;
    }

    start();
}

void KnowledgePrewarmer::processNextTrack()
{
    if (!m_running) {
        return;
    }

    if (m_currentTrackIndex >= m_pendingTracks.size()) {
        finishRound();
        return;
    }

    m_currentStep = Step::Artist;
    processStep();
}

void KnowledgePrewarmer::processStep()
{
    if (!m_running || m_currentTrackIndex >= m_pendingTracks.size()) {
        return;
    }

    const auto &track = m_pendingTracks.at(m_currentTrackIndex);

    switch (m_currentStep) {
    case Step::Artist: {
        const KnowledgeQuery artistQuery {
            .kind = KnowledgeKind::Artist,
            .artist = track.artist,
            .title = { },
        };
        m_pendingKey = knowledgeKey(artistQuery);
        m_knowledgeService.lookup(artistQuery);
        break;
    }
    case Step::Song: {
        const KnowledgeQuery songQuery {
            .kind = KnowledgeKind::Song,
            .artist = track.artist,
            .title = track.title,
        };
        m_pendingKey = knowledgeKey(songQuery);
        m_knowledgeService.lookup(songQuery);
        break;
    }
    case Step::Album: {
        if (track.album.trimmed().isEmpty()) {
            m_consecutiveFailures = 0;
            ++m_currentTrackIndex;
            QTimer::singleShot(0, this, &KnowledgePrewarmer::processNextTrack);
            return;
        }
        const KnowledgeQuery albumQuery {
            .kind = KnowledgeKind::Album,
            .artist = track.artist,
            .title = track.album,
        };
        m_pendingKey = knowledgeKey(albumQuery);
        m_knowledgeService.lookup(albumQuery);
        break;
    }
    }
}

// 推进到下一步一律排队执行：缓存命中时 lookup 会同步回调，直接调用会形成递归、加深调用栈
void KnowledgePrewarmer::onKnowledgeFinished(const KnowledgeResult &result)
{
    if (!m_running) {
        return;
    }

    if (result.key.isEmpty() || result.key != m_pendingKey) {
        return;
    }

    m_pendingKey.clear();

    if (result.status == KnowledgeStatus::Failed) {
        ++m_consecutiveFailures;
        if (m_consecutiveFailures >= 3) {
            finishRound();
            return;
        }
        ++m_currentTrackIndex;
        QTimer::singleShot(0, this, &KnowledgePrewarmer::processNextTrack);
        return;
    }

    switch (m_currentStep) {
    case Step::Artist:
        m_currentStep = Step::Song;
        QTimer::singleShot(0, this, &KnowledgePrewarmer::processStep);
        break;
    case Step::Song:
        m_currentStep = Step::Album;
        QTimer::singleShot(0, this, &KnowledgePrewarmer::processStep);
        break;
    case Step::Album:
        m_consecutiveFailures = 0;
        ++m_currentTrackIndex;
        QTimer::singleShot(0, this, &KnowledgePrewarmer::processNextTrack);
        break;
    }
}

void KnowledgePrewarmer::finishRound()
{
    m_pendingTracks.clear();
    m_pendingKey.clear();
    m_currentTrackIndex = 0;
    m_consecutiveFailures = 0;

    m_settings.setValue(kGuideLastPrewarmAt, m_clock.nowMs());

    if (m_running) {
        m_running = false;
        emit runningChanged();
    }
}

} // namespace linernotes::guide
