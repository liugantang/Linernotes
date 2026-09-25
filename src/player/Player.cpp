// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Player.h"

#include "PlayQueue.h"
#include "PlayerLogging.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace linernotes::player {

namespace {
constexpr double kRestartThresholdSeconds = 3.0;
} // namespace

Player::Player(const MpvHandle::OptionList &extraOptions, QObject *parent)
    : QObject(parent)
    , m_mpv(new MpvHandle(extraOptions, this))
{
    std::random_device rd;
    const quint64 seed = (static_cast<quint64>(rd()) << 32) | static_cast<quint64>(rd());
    m_queue = new PlayQueue(seed, this);

    connect(m_queue, &PlayQueue::upcomingChanged, this, &Player::onUpcomingChanged);

    if (m_mpv == nullptr || !m_mpv->isValid()) {
        qCWarning(lcPlayer) << "Player initialization failed because MpvHandle is invalid:"
                            << ((m_mpv != nullptr) ? m_mpv->errorString()
                                                   : QStringLiteral("null handle"));
        return;
    }

    m_mpv->observeProperty(QStringLiteral("idle-active"));
    m_mpv->observeProperty(QStringLiteral("pause"));
    m_mpv->observeProperty(QStringLiteral("time-pos"));
    m_mpv->observeProperty(QStringLiteral("duration"));
    m_mpv->observeProperty(QStringLiteral("volume"));
    m_mpv->observeProperty(QStringLiteral("mute"));

    const QVariant idleVar = m_mpv->property(QStringLiteral("idle-active"));
    if (idleVar.isValid()) {
        m_idleActive = idleVar.toBool();
    }
    const QVariant pauseVar = m_mpv->property(QStringLiteral("pause"));
    if (pauseVar.isValid()) {
        m_pause = pauseVar.toBool();
    }
    const QVariant volVar = m_mpv->property(QStringLiteral("volume"));
    if (volVar.isValid()) {
        m_volume = std::clamp(static_cast<int>(std::round(volVar.toDouble())), 0, 100);
    }
    const QVariant muteVar = m_mpv->property(QStringLiteral("mute"));
    if (muteVar.isValid()) {
        m_muted = muteVar.toBool();
    }

    if (m_idleActive) {
        m_state = PlaybackState::Stopped;
    } else if (m_pause) {
        m_state = PlaybackState::Paused;
    } else {
        m_state = PlaybackState::Playing;
    }

    connect(m_mpv, &MpvHandle::propertyChanged, this, &Player::onPropertyChanged);
    connect(m_mpv, &MpvHandle::startFile, this, &Player::onStartFile);
    connect(m_mpv, &MpvHandle::endFile, this, &Player::onEndFile);
}

PlayQueue *Player::queue() const
{
    return m_queue;
}

bool Player::isValid() const
{
    return m_mpv != nullptr && m_mpv->isValid();
}

Player::PlaybackState Player::state() const
{
    return m_state;
}

double Player::position() const
{
    return m_position;
}

double Player::duration() const
{
    return m_duration;
}

int Player::volume() const
{
    return m_volume;
}

bool Player::isMuted() const
{
    return m_muted;
}

QString Player::currentSource() const
{
    return m_currentSource;
}

int Player::mpvPlaylistCount() const
{
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return 0;
    }
    const QVariant val = m_mpv->property(QStringLiteral("playlist-count"));
    return val.isValid() ? val.toInt() : 0;
}

void Player::openFile(const QString &path)
{
    qCDebug(lcPlayer) << "openFile:" << path;
    m_queue->setItems({ { .source = path } }, -1);
    playIndex(0);
}

void Player::playIndex(int row)
{
    qCDebug(lcPlayer) << "playIndex:" << row;
    if (row < 0 || row >= m_queue->count()) {
        return;
    }
    const auto item = m_queue->jumpTo(row);
    if (item.has_value()) {
        loadCurrentItem(*item);
    }
}

void Player::next()
{
    qCDebug(lcPlayer) << "next() called";
    const auto nextItem = m_queue->advance(PlayOrder::Advance::User);
    if (nextItem.has_value()) {
        loadCurrentItem(*nextItem);
    } else {
        stop();
    }
}

void Player::previous()
{
    qCDebug(lcPlayer) << "previous() called, position:" << m_position;
    if (m_position > kRestartThresholdSeconds) {
        seek(0.0);
    } else {
        const auto prevItem = m_queue->previous();
        if (prevItem.has_value()) {
            loadCurrentItem(*prevItem);
        } else {
            seek(0.0);
        }
    }
}

void Player::play()
{
    qCDebug(lcPlayer) << "play() called, current state:" << static_cast<int>(m_state)
                      << "queue count:" << m_queue->count();
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }

    if (m_queue->count() == 0) {
        return;
    }

    if (m_state == PlaybackState::Paused) {
        m_mpv->setProperty(QStringLiteral("pause"), false);
    } else if (m_state == PlaybackState::Stopped) {
        const auto cur = m_queue->currentItem();
        if (cur.has_value()) {
            loadCurrentItem(*cur);
        } else {
            const auto nextItem = m_queue->advance(PlayOrder::Advance::Auto);
            if (nextItem.has_value()) {
                loadCurrentItem(*nextItem);
            }
        }
    }
}

void Player::pause()
{
    qCDebug(lcPlayer) << "pause() called";
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    if (m_state == PlaybackState::Playing) {
        m_mpv->setProperty(QStringLiteral("pause"), true);
    }
}

void Player::togglePause()
{
    qCDebug(lcPlayer) << "togglePause() called";
    if (m_state == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void Player::stop()
{
    qCDebug(lcPlayer) << "stop() called";
    m_preloadEntryId = -1;
    m_preloadUid = 0;
    m_preloadIsRepeatOne = false;
    m_currentEntryId = -1;
    m_currentUid = 0;

    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->command({ QStringLiteral("stop") });
    m_mpv->command({ QStringLiteral("playlist-clear") });
}

void Player::seek(double seconds)
{
    qCDebug(lcPlayer) << "seek() to" << seconds;
    if (m_state == PlaybackState::Stopped) {
        return;
    }
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->command(
        { QStringLiteral("seek"), QString::number(seconds, 'f', 6), QStringLiteral("absolute") });
}

void Player::setVolume(int volume)
{
    const int clamped = std::clamp(volume, 0, 100);
    if (clamped == m_volume) {
        return;
    }
    qCDebug(lcPlayer) << "setVolume() to" << clamped;
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->setProperty(QStringLiteral("volume"), static_cast<double>(clamped));
}

void Player::setMuted(bool muted)
{
    if (muted == m_muted) {
        return;
    }
    qCDebug(lcPlayer) << "setMuted() to" << muted;
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->setProperty(QStringLiteral("mute"), muted);
}

void Player::handleIdleActiveChanged(const QVariant &value)
{
    const bool idle = value.isValid() ? value.toBool() : true;
    if (m_idleActive != idle) {
        m_idleActive = idle;
        updatePlaybackState();
    }
}

void Player::handlePauseChanged(const QVariant &value)
{
    const bool paused = value.isValid() ? value.toBool() : false;
    if (m_pause != paused) {
        m_pause = paused;
        updatePlaybackState();
    }
}

void Player::handleTimePosChanged(const QVariant &value)
{
    const double pos
        = (!m_idleActive && value.isValid() && !value.isNull()) ? value.toDouble() : 0.0;
    m_position = pos;
    if (m_position == 0.0) {
        if (m_lastEmittedPosition != 0.0) {
            m_lastEmittedPosition = 0.0;
            emit positionChanged(0.0);
        }
    } else if (std::abs(m_position - m_lastEmittedPosition) >= 0.1) {
        m_lastEmittedPosition = m_position;
        emit positionChanged(m_position);
    }
}

void Player::handleDurationChanged(const QVariant &value)
{
    const double dur
        = (!m_idleActive && value.isValid() && !value.isNull()) ? value.toDouble() : 0.0;
    if (std::abs(dur - m_duration) > 1e-6) {
        m_duration = dur;
        emit durationChanged(m_duration);
    }
}

void Player::handleVolumeChanged(const QVariant &value)
{
    if (value.isValid() && !value.isNull()) {
        const int vol = std::clamp(static_cast<int>(std::round(value.toDouble())), 0, 100);
        if (vol != m_volume) {
            m_volume = vol;
            emit volumeChanged(m_volume);
        }
    }
}

void Player::handleMuteChanged(const QVariant &value)
{
    if (value.isValid() && !value.isNull()) {
        const bool muted = value.toBool();
        if (muted != m_muted) {
            m_muted = muted;
            emit mutedChanged(m_muted);
        }
    }
}

void Player::onPropertyChanged(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("idle-active")) {
        handleIdleActiveChanged(value);
    } else if (name == QStringLiteral("pause")) {
        handlePauseChanged(value);
    } else if (name == QStringLiteral("time-pos")) {
        handleTimePosChanged(value);
    } else if (name == QStringLiteral("duration")) {
        handleDurationChanged(value);
    } else if (name == QStringLiteral("volume")) {
        handleVolumeChanged(value);
    } else if (name == QStringLiteral("mute")) {
        handleMuteChanged(value);
    }
}

void Player::updatePlaybackState()
{
    PlaybackState newState = PlaybackState::Stopped;
    if (m_mpv != nullptr && m_mpv->isValid() && !m_idleActive) {
        newState = m_pause ? PlaybackState::Paused : PlaybackState::Playing;
    }

    if (newState != m_state) {
        m_state = newState;
        if (m_state == PlaybackState::Stopped) {
            m_position = 0.0;
            if (m_lastEmittedPosition != 0.0) {
                m_lastEmittedPosition = 0.0;
                emit positionChanged(0.0);
            }
            if (m_duration != 0.0) {
                m_duration = 0.0;
                emit durationChanged(0.0);
            }
        }
        qCDebug(lcPlayer) << "Playback state changed to" << static_cast<int>(m_state);
        emit stateChanged(m_state);
    }
}

// ============================================================================
// Gapless 预加载与播放列表同步核心设计说明
//
// 1. 不变式：mpv 的 playlist 最多包含 2 项：[正在播放项, 预加载项]。
//    Player 通过 m_currentEntryId / m_currentUid 与 m_preloadEntryId / m_preloadUid 跟踪两项。
// 2. 加载当前项：使用 `loadfile <path> replace` 覆盖原有列表并播放。
//    加载预加载项：使用 `loadfile <path> append` 追加到末尾供 mpv 预读与无缝解码。
// 3. 自然切换：当监听到 mpv 的 startFile 事件且 entryId == m_preloadEntryId 时，说明 mpv
//    已无缝切换到预加载项。此时调用 queue()->advance(Auto) 推进队列，更新当前项并移除旧项
//    （`playlist-remove 0`），随后触发下一轮预加载。
// 4. 预加载同步：连接 PlayQueue::upcomingChanged 信号，合并排队调用 syncPreload() 计算
//    queue()->peekNext(Auto)。若与当前预加载项不符，则通过 `playlist-remove 1` 替换为新预加载项。
// 5. 单曲循环（RepeatOne）：peekNext(Auto) 仍返回当前项，照常 append 同一文件以实现无缝单曲循环。
// ============================================================================

void Player::loadCurrentItem(const QueueItem &item)
{
    m_inInternalSync = true;
    m_preloadEntryId = -1;
    m_preloadUid = 0;
    m_preloadIsRepeatOne = false;
    m_currentUid = item.uid;

    if (m_currentSource != item.source) {
        m_currentSource = item.source;
        emit currentSourceChanged(m_currentSource);
    }

    if (m_mpv != nullptr && m_mpv->isValid()) {
        m_mpv->command({ QStringLiteral("loadfile"), item.source, QStringLiteral("replace") });
        m_mpv->setProperty(QStringLiteral("pause"), false);
        m_currentEntryId = lastPlaylistEntryId();
    }
    m_inInternalSync = false;

    schedulePreloadSync();
}

qint64 Player::lastPlaylistEntryId() const
{
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return -1;
    }
    const QVariant var = m_mpv->property(QStringLiteral("playlist"));
    const QVariantList list = var.toList();
    if (!list.isEmpty()) {
        const QVariantMap map = list.last().toMap();
        return map.value(QStringLiteral("id")).toLongLong();
    }
    return -1;
}

void Player::onStartFile(qint64 entryId)
{
    if (entryId == m_currentEntryId) {
        qCDebug(lcPlayer) << "startFile for current entry:" << entryId;
        schedulePreloadSync();
        return;
    }

    if (entryId == m_preloadEntryId && m_preloadEntryId != -1) {
        qCDebug(lcPlayer) << "Seamless transition to preloaded entry:" << entryId;
        const auto advItem = m_queue->advance(PlayOrder::Advance::Auto);
        if (!advItem.has_value() || advItem->uid != m_preloadUid) {
            qCWarning(lcPlayer) << "Natural transition mismatch: expected preload uid"
                                << m_preloadUid << "got" << (advItem ? advItem->uid : 0);
            if (advItem.has_value()) {
                loadCurrentItem(*advItem);
            } else {
                stop();
            }
            return;
        }

        m_currentUid = m_preloadUid;
        m_currentEntryId = m_preloadEntryId;
        m_preloadUid = 0;
        m_preloadEntryId = -1;
        m_preloadIsRepeatOne = false;

        if (m_currentSource != advItem->source) {
            m_currentSource = advItem->source;
            emit currentSourceChanged(m_currentSource);
        }

        if (m_mpv != nullptr && m_mpv->isValid()) {
            m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("0") });
        }

        schedulePreloadSync();
    }
}

void Player::onEndFile(qint64 entryId, MpvHandle::EndFileReason reason, const QString &error)
{
    qCDebug(lcPlayer) << "endFile:" << entryId << "reason:" << static_cast<int>(reason)
                      << "error:" << error;
    if (reason == MpvHandle::EndFileReason::Eof) {
        if (m_preloadEntryId == -1 && (entryId == m_currentEntryId || m_currentEntryId == -1)) {
            m_queue->advance(PlayOrder::Advance::Auto);
            m_currentEntryId = -1;
            m_currentUid = 0;
            emit playbackFinished();
        }
    } else if (reason == MpvHandle::EndFileReason::Error) {
        qCWarning(lcPlayer) << "Playback error on entry" << entryId << ":" << error;
        if (m_preloadEntryId == -1 && (entryId == m_currentEntryId || m_currentEntryId == -1)) {
            m_currentEntryId = -1;
            m_currentUid = 0;
        }
    }
}

void Player::onUpcomingChanged()
{
    schedulePreloadSync();
}

void Player::schedulePreloadSync()
{
    if (m_preloadSyncPending) {
        return;
    }
    m_preloadSyncPending = true;
    QMetaObject::invokeMethod(this, &Player::syncPreload, Qt::QueuedConnection);
}

void Player::syncPreload()
{
    m_preloadSyncPending = false;

    if (m_inInternalSync || m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }

    if (m_currentEntryId == -1 || m_state == PlaybackState::Stopped) {
        if (m_preloadEntryId != -1) {
            m_inInternalSync = true;
            m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
            m_preloadEntryId = -1;
            m_preloadUid = 0;
            m_preloadIsRepeatOne = false;
            m_inInternalSync = false;
        }
        return;
    }

    const auto targetNext = m_queue->peekNext(PlayOrder::Advance::Auto);
    const bool isRepeatOne = (m_queue->mode() == PlayMode::RepeatOne);

    if (!targetNext.has_value()) {
        if (m_preloadEntryId != -1) {
            m_inInternalSync = true;
            m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
            m_preloadEntryId = -1;
            m_preloadUid = 0;
            m_preloadIsRepeatOne = false;
            m_inInternalSync = false;
        }
        return;
    }

    const QueueItem &nextItem = *targetNext;

    if (m_preloadEntryId != -1 && m_preloadUid == nextItem.uid
        && m_preloadIsRepeatOne == isRepeatOne) {
        return;
    }

    m_inInternalSync = true;
    if (m_preloadEntryId != -1) {
        m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
        m_preloadEntryId = -1;
        m_preloadUid = 0;
        m_preloadIsRepeatOne = false;
    }

    m_mpv->command({ QStringLiteral("loadfile"), nextItem.source, QStringLiteral("append") });
    m_preloadEntryId = lastPlaylistEntryId();
    m_preloadUid = nextItem.uid;
    m_preloadIsRepeatOne = isRepeatOne;
    m_inInternalSync = false;
}

} // namespace linernotes::player
