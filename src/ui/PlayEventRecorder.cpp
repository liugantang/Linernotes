// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "PlayEventRecorder.h"

#include "UiLogging.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <library/LibraryQuery.h>

#include <chrono>
#include <cmath>
#include <utility>

namespace linernotes::ui {

PlayEventRecorder::PlayEventRecorder(
    player::Player &player, library::Database &db, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_db(db)
    , m_clock(clock)
    , m_store(db)
{
    connect(&m_player, &player::Player::trackStarted, this, &PlayEventRecorder::onTrackStarted);
    connect(&m_player, &player::Player::stateChanged, this, &PlayEventRecorder::onStateChanged);
    connect(
        &m_player, &player::Player::positionChanged, this, &PlayEventRecorder::onPositionChanged);
    connect(
        &m_player, &player::Player::durationChanged, this, &PlayEventRecorder::onDurationChanged);

    connect(&m_checkpointTimer, &QTimer::timeout, this, &PlayEventRecorder::onCheckpointTimer);
    m_checkpointTimer.setInterval(std::chrono::seconds(30));
    m_checkpointTimer.start();
}

// DB writes return Result and do not throw; Qt/std memory exhaustion is unrecoverable.
// NOLINTNEXTLINE(bugprone-exception-escape)
PlayEventRecorder::~PlayEventRecorder()
{
    endCurrentSession(player::ListenEnd::Stopped);
}

void PlayEventRecorder::onTrackStarted(const linernotes::player::QueueItem &item)
{
    if (m_current.has_value()) {
        endCurrentSession(player::ListenEnd::Switched);
    }

    qint64 durationMs = 0;
    QString path = item.source;
    QString title;
    QString artist;
    QString album;

    if (item.trackId > 0 && m_db.isOpen()) {
        const auto connRes = m_db.connection();
        if (connRes.ok()) {
            const library::LibraryQuery query(connRes.value());
            const auto res = query.tracksByIds({ item.trackId });
            if (res.ok() && !res.value().isEmpty()) {
                const auto &row = res.value().constFirst();
                durationMs = row.durationMs;
                path = row.path;
                title = row.title;
                artist = row.artist;
                album = row.album;
            }
        }
    }

    QJsonObject snapObj;
    snapObj.insert(QStringLiteral("path"), path);
    snapObj.insert(QStringLiteral("title"), title);
    snapObj.insert(QStringLiteral("artist"), artist);
    snapObj.insert(QStringLiteral("album"), album);
    const QString snapshotJson
        = QString::fromUtf8(QJsonDocument(snapObj).toJson(QJsonDocument::Compact));

    player::ListenSession session(durationMs);
    if (m_player.state() == player::Player::PlaybackState::Playing) {
        session.onPlaying(true, m_clock.nowMs());
    }

    m_current = CurrentSession {
        .item = item,
        .session = session,
        .device = m_player.audioDevice(),
        .snapshotJson = snapshotJson,
        .eventId = -1,
    };
}

void PlayEventRecorder::onStateChanged(player::Player::PlaybackState state)
{
    if (!m_current.has_value()) {
        return;
    }

    if (state == player::Player::PlaybackState::Playing) {
        m_current->session.onPlaying(true, m_clock.nowMs());
    } else if (state == player::Player::PlaybackState::Paused) {
        m_current->session.onPlaying(false, m_clock.nowMs());
        checkpoint();
    } else if (state == player::Player::PlaybackState::Stopped) {
        endCurrentSession(player::ListenEnd::Stopped);
    }
}

void PlayEventRecorder::onPositionChanged(double position)
{
    if (!m_current.has_value()) {
        return;
    }
    const auto posMs = static_cast<qint64>(std::round(position * 1000.0));
    m_current->session.onPosition(posMs, m_clock.nowMs());
}

void PlayEventRecorder::onDurationChanged(double duration)
{
    if (!m_current.has_value() || duration <= 0.0) {
        return;
    }
    const auto durMs = static_cast<qint64>(std::round(duration * 1000.0));
    m_current->session.setDuration(durMs);
}

void PlayEventRecorder::onCheckpointTimer()
{
    if (m_current.has_value() && m_player.state() == player::Player::PlaybackState::Playing) {
        checkpoint();
    }
}

void PlayEventRecorder::checkpoint()
{
    syncToStore(false);
}

void PlayEventRecorder::syncToStore(bool ended, player::ListenEnd end)
{
    if (!m_current.has_value()) {
        return;
    }

    const qint64 now = m_clock.nowMs();
    const auto res = m_current->session.result(end, now);
    if (!m_current->session.hasStarted() || res.playedMs <= 0) {
        return;
    }

    library::PlayEvent event;
    event.id = m_current->eventId;
    if (m_current->item.trackId > 0) {
        event.trackId = m_current->item.trackId;
    }
    event.startedAtMs = res.startedAtMs;
    if (ended) {
        event.endedAtMs = res.endedAtMs;
    }
    event.playedMs = res.playedMs;
    event.pausedMs = res.pausedMs;
    if (res.durationMs > 0) {
        event.durationMs = res.durationMs;
    }
    event.completed = res.completed;
    event.skipped = res.skipped;
    event.skipPositionMs = res.skipPositionMs;
    event.playSource = m_current->item.playSource;
    event.device = m_current->device;
    event.snapshot = m_current->snapshotJson;

    if (m_current->eventId == -1) {
        const auto insertRes = m_store.insert(event);
        if (insertRes.ok()) {
            m_current->eventId = insertRes.value();
        } else {
            qCWarning(lcUi) << "Failed to insert play event:" << insertRes.error().toString();
        }
    } else {
        const auto updateRes = m_store.update(event);
        if (!updateRes.ok()) {
            qCWarning(lcUi) << "Failed to update play event:" << updateRes.error().toString();
        }
    }
}

void PlayEventRecorder::endCurrentSession(player::ListenEnd end)
{
    if (!m_current.has_value()) {
        return;
    }
    syncToStore(true, end);
    m_current.reset();
}

} // namespace linernotes::ui
