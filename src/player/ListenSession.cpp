// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ListenSession.h"

#include <algorithm>

namespace linernotes::player {

ListenSession::ListenSession(qint64 durationMs)
    : m_durationMs(std::max<qint64>(0, durationMs))
{
}

void ListenSession::setDuration(qint64 durationMs)
{
    m_durationMs = std::max<qint64>(0, durationMs);
}

void ListenSession::onPlaying(bool playing, qint64 nowMs)
{
    if (m_isPlaying == playing) {
        return;
    }
    m_isPlaying = playing;

    if (playing) {
        if (m_startedAtMs == 0) {
            m_startedAtMs = nowMs;
        } else if (m_pausedSinceMs.has_value()) {
            m_accumulatedPausedMs += (nowMs - *m_pausedSinceMs);
            m_pausedSinceMs.reset();
        }
        m_lastPositionWallMs = nowMs;
    } else {
        if (m_startedAtMs > 0 && !m_pausedSinceMs.has_value()) {
            m_pausedSinceMs = nowMs;
        }
    }
}

void ListenSession::onPosition(qint64 positionMs, qint64 nowMs)
{
    if (positionMs <= 0) {
        return;
    }

    m_latestPositionMs = positionMs;

    if (m_isPlaying && m_startedAtMs > 0) {
        if (m_lastPositionMs.has_value() && m_lastPositionWallMs.has_value()) {
            const qint64 deltaPos = positionMs - *m_lastPositionMs;
            const qint64 deltaWall = nowMs - *m_lastPositionWallMs;
            if (deltaPos > 0 && deltaPos <= (deltaWall + 1000)) {
                m_playedMs += deltaPos;
            }
        }
        m_lastPositionMs = positionMs;
        m_lastPositionWallMs = nowMs;
    } else {
        m_lastPositionMs = positionMs;
        m_lastPositionWallMs = nowMs;
    }
}

bool ListenSession::hasStarted() const
{
    return m_startedAtMs > 0;
}

ListenResult ListenSession::result(ListenEnd end, qint64 nowMs) const
{
    ListenResult res;
    res.startedAtMs = m_startedAtMs;
    res.endedAtMs = nowMs;
    res.playedMs = m_playedMs;
    res.durationMs = m_durationMs;

    qint64 totalPaused = m_accumulatedPausedMs;
    if (m_startedAtMs > 0 && m_pausedSinceMs.has_value()) {
        totalPaused += (nowMs - *m_pausedSinceMs);
    }
    res.pausedMs = totalPaused;

    if (m_durationMs > 0 && m_latestPositionMs.has_value()) {
        res.completed = (*m_latestPositionMs >= m_durationMs - 3000);
    } else {
        res.completed = false;
    }

    if (end == ListenEnd::Switched && !res.completed) {
        res.skipped = true;
        res.skipPositionMs = m_latestPositionMs;
    } else {
        res.skipped = false;
        res.skipPositionMs = std::nullopt;
    }

    return res;
}

} // namespace linernotes::player
