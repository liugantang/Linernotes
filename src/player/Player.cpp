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

QString extractUserAudioFilters(const MpvHandle::OptionList &extraOptions)
{
    for (const auto &opt : extraOptions) {
        if (opt.first == QStringLiteral("af")) {
            return opt.second;
        }
    }
    return { };
}

MpvHandle::OptionList buildPlayerOptions(const MpvHandle::OptionList &extraOptions)
{
    MpvHandle::OptionList options = extraOptions;
    const QString userAf = extractUserAudioFilters(extraOptions);
    const QString duckFilter = QStringLiteral("@duck:lavfi=[volume=volume=1.0]");
    const QString combinedAf
        = userAf.isEmpty() ? duckFilter : (userAf + QStringLiteral(",") + duckFilter);

    auto it = std::ranges::find_if(
        options, [](const auto &opt) { return opt.first == QStringLiteral("af"); });
    if (it != options.end()) {
        it->second = combinedAf;
    } else {
        options.append({ QStringLiteral("af"), combinedAf });
    }
    return options;
}

quint64 generateQueueSeed()
{
    std::random_device rd;
    return (static_cast<quint64>(rd()) << 32) | static_cast<quint64>(rd());
}
} // namespace

Player::Player(const MpvHandle::OptionList &extraOptions, QObject *parent)
    : QObject(parent)
    , m_mpv(buildPlayerOptions(extraOptions))
    , m_queue(generateQueueSeed())
    , m_ducker(m_mpv, extractUserAudioFilters(extraOptions))
    , m_audioOutput(m_mpv)
{
    connect(&m_ducker, &Ducker::duckGainChanged, this, &Player::duckGainChanged);
    connect(&m_ducker, &Ducker::duckFinished, this, &Player::duckFinished);

    connect(&m_audioOutput, &AudioOutput::audioDevicesChanged, this, &Player::audioDevicesChanged);
    connect(&m_audioOutput, &AudioOutput::audioDeviceChanged, this, &Player::audioDeviceChanged);
    connect(
        &m_audioOutput, &AudioOutput::exclusiveModeChanged, this, &Player::exclusiveModeChanged);

    connect(&m_queue, &PlayQueue::upcomingChanged, this, &Player::onUpcomingChanged);

    if (!m_mpv.isValid()) {
        qCWarning(lcPlayer) << "Player initialization failed because MpvHandle is invalid:"
                            << m_mpv.errorString();
        return;
    }

    for (const auto &prop :
        { QStringLiteral("idle-active"), QStringLiteral("pause"), QStringLiteral("time-pos"),
            QStringLiteral("duration"), QStringLiteral("volume"), QStringLiteral("mute") }) {
        m_mpv.observeProperty(prop);
    }

    const QVariant idleVar = m_mpv.property(QStringLiteral("idle-active"));
    if (idleVar.isValid()) {
        m_idleActive = idleVar.toBool();
    }
    const QVariant pauseVar = m_mpv.property(QStringLiteral("pause"));
    if (pauseVar.isValid()) {
        m_pause = pauseVar.toBool();
    }
    const QVariant volVar = m_mpv.property(QStringLiteral("volume"));
    if (volVar.isValid()) {
        m_volume = std::clamp(static_cast<int>(std::round(volVar.toDouble())), 0, 100);
    }
    const QVariant muteVar = m_mpv.property(QStringLiteral("mute"));
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

    connect(&m_mpv, &MpvHandle::propertyChanged, this, &Player::onPropertyChanged);
    connect(&m_mpv, &MpvHandle::startFile, this, &Player::onStartFile);
    connect(&m_mpv, &MpvHandle::fileLoaded, this, &Player::onFileLoaded);
    connect(&m_mpv, &MpvHandle::endFile, this, &Player::onEndFile);
}

PlayQueue *Player::queue()
{
    return &m_queue;
}

bool Player::isValid() const
{
    return m_mpv.isValid();
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

double Player::duckGain() const
{
    return m_ducker.duckGain();
}

QString Player::currentSource() const
{
    return m_currentSource;
}

PlaybackSnapshot Player::snapshot() const
{
    PlaybackSnapshot snap;
    const int count = m_queue.count();
    snap.items.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto &item = m_queue.at(i);
        snap.items.append({ .source = item.source, .trackId = item.trackId });
    }
    snap.currentIndex = m_queue.currentIndex();
    snap.mode = m_queue.mode();
    snap.position
        = (snap.currentIndex != -1 && m_state != PlaybackState::Stopped) ? m_position : 0.0;
    snap.volume = m_volume;
    snap.muted = m_muted;
    snap.audioDevice = m_audioOutput.audioDevice();
    return snap;
}

void Player::restore(const PlaybackSnapshot &snapshot)
{
    qCDebug(lcPlayer) << "restore() called: items count:" << snapshot.items.size()
                      << "currentIndex:" << snapshot.currentIndex
                      << "position:" << snapshot.position
                      << "mode:" << static_cast<int>(snapshot.mode) << "volume:" << snapshot.volume
                      << "muted:" << snapshot.muted << "audioDevice:" << snapshot.audioDevice;

    setVolume(snapshot.volume);
    setMuted(snapshot.muted);

    if (snapshot.audioDevice.isEmpty() || snapshot.audioDevice == QStringLiteral("auto")) {
        if (m_audioOutput.audioDevice() != QStringLiteral("auto")) {
            m_audioOutput.selectAudioDevice(QStringLiteral("auto"));
        }
    } else {
        if (!m_audioOutput.isDevicesRefreshed()) {
            m_audioOutput.refreshAudioDevices();
        }
        const auto &devices = m_audioOutput.audioDevices();
        const bool deviceFound = std::ranges::any_of(devices, [&](const QVariant &dev) {
            return dev.toMap().value(QStringLiteral("name")).toString() == snapshot.audioDevice;
        });
        if (deviceFound) {
            m_audioOutput.selectAudioDevice(snapshot.audioDevice);
        } else {
            qCInfo(lcPlayer) << "Snapshot audio device" << snapshot.audioDevice
                             << "not found in audioDevices list, keeping auto";
            if (m_audioOutput.audioDevice() != QStringLiteral("auto")) {
                m_audioOutput.selectAudioDevice(QStringLiteral("auto"));
            }
        }
    }

    m_consecutiveErrorCount = 0;
    stop();

    m_queue.setMode(snapshot.mode);

    QList<QueueItem> queueItems;
    queueItems.reserve(snapshot.items.size());
    for (const auto &item : snapshot.items) {
        queueItems.append({ .source = item.source, .trackId = item.trackId });
    }

    const int targetIndex
        = (snapshot.currentIndex >= 0 && snapshot.currentIndex < queueItems.size())
        ? snapshot.currentIndex
        : -1;
    m_queue.setItems(queueItems, targetIndex);

    if (targetIndex != -1) {
        loadItem(m_queue.at(targetIndex), snapshot.position);
    }
}

int Player::mpvPlaylistCount() const
{
    if (!m_mpv.isValid()) {
        return 0;
    }
    const QVariant val = m_mpv.property(QStringLiteral("playlist-count"));
    return val.isValid() ? val.toInt() : 0;
}

QVariant Player::mpvAudioFilters() const
{
    if (!m_mpv.isValid()) {
        return { };
    }
    return m_mpv.property(QStringLiteral("af"));
}

int Player::duckApplyCount() const
{
    return m_ducker.duckApplyCount();
}

void Player::openFile(const QString &path)
{
    qCDebug(lcPlayer) << "openFile:" << path;
    m_consecutiveErrorCount = 0;
    m_queue.setItems({ { .source = path } }, -1);
    playIndex(0);
}

void Player::playIndex(int row)
{
    qCDebug(lcPlayer) << "playIndex:" << row;
    m_consecutiveErrorCount = 0;
    if (row < 0 || row >= m_queue.count()) {
        return;
    }
    const auto item = m_queue.jumpTo(row);
    if (item.has_value()) {
        loadItem(*item);
    }
}

void Player::next()
{
    qCDebug(lcPlayer) << "next() called";
    m_consecutiveErrorCount = 0;
    const auto nextItem = m_queue.advance(PlayOrder::Advance::User);
    if (nextItem.has_value()) {
        loadItem(*nextItem);
    } else {
        stop();
    }
}

void Player::previous()
{
    qCDebug(lcPlayer) << "previous() called, position:" << m_position;
    m_consecutiveErrorCount = 0;
    if (m_position > kRestartThresholdSeconds) {
        seek(0.0);
        return;
    }
    const auto prevItem = m_queue.previous();
    if (prevItem.has_value()) {
        loadItem(*prevItem);
    } else {
        seek(0.0);
    }
}

void Player::play()
{
    qCDebug(lcPlayer) << "play() called, current state:" << static_cast<int>(m_state)
                      << "queue count:" << m_queue.count();
    m_consecutiveErrorCount = 0;
    if (!m_mpv.isValid() || m_queue.count() == 0) {
        return;
    }

    if (m_state == PlaybackState::Paused) {
        m_mpv.setProperty(QStringLiteral("pause"), false);
    } else if (m_state == PlaybackState::Stopped) {
        const auto cur = m_queue.currentItem();
        if (cur.has_value()) {
            loadItem(*cur);
        } else {
            const auto nextItem = m_queue.advance(PlayOrder::Advance::Auto);
            if (nextItem.has_value()) {
                loadItem(*nextItem);
            }
        }
    }
}

void Player::pause()
{
    qCDebug(lcPlayer) << "pause() called";
    if (m_mpv.isValid() && m_state == PlaybackState::Playing) {
        m_mpv.setProperty(QStringLiteral("pause"), true);
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
    if (m_currentEntryId != -1) {
        m_ignoredEntryIds.insert(m_currentEntryId);
    }
    discardPreload(false);
    m_currentEntryId = -1;
    m_currentUid = 0;
    m_entrySources.clear();
    m_consecutiveErrorCount = 0;

    if (!m_mpv.isValid()) {
        return;
    }
    m_mpv.command({ QStringLiteral("stop") });
    m_mpv.command({ QStringLiteral("playlist-clear") });
}

void Player::seek(double seconds)
{
    qCDebug(lcPlayer) << "seek() to" << seconds;
    if (m_state != PlaybackState::Stopped && m_mpv.isValid()) {
        m_mpv.command({ QStringLiteral("seek"), QString::number(seconds, 'f', 6),
            QStringLiteral("absolute") });
    }
}

void Player::setVolume(int volume)
{
    const int clamped = std::clamp(volume, 0, 100);
    if (clamped != m_volume) {
        qCDebug(lcPlayer) << "setVolume() to" << clamped;
        if (m_mpv.isValid()) {
            m_mpv.setProperty(QStringLiteral("volume"), static_cast<double>(clamped));
        }
    }
}

void Player::setMuted(bool muted)
{
    if (muted != m_muted) {
        qCDebug(lcPlayer) << "setMuted() to" << muted;
        if (m_mpv.isValid()) {
            m_mpv.setProperty(QStringLiteral("mute"), muted);
        }
    }
}

void Player::duckTo(double gain, int rampMs)
{
    m_ducker.duckTo(gain, rampMs);
}

void Player::unduck(int rampMs)
{
    m_ducker.unduck(rampMs);
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
    if (m_mpv.isValid() && !m_idleActive) {
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

void Player::discardPreload(bool removeFromMpv)
{
    if (m_preloadEntryId == -1) {
        return;
    }
    const bool wasSyncing = m_inInternalSync;
    m_inInternalSync = true;
    m_ignoredEntryIds.insert(m_preloadEntryId);
    if (removeFromMpv && m_mpv.isValid()) {
        m_mpv.command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
    }
    m_entrySources.remove(m_preloadEntryId);
    m_preloadEntryId = -1;
    m_preloadUid = 0;
    m_preloadIsRepeatOne = false;
    m_inInternalSync = wasSyncing;
}

void Player::loadItem(const QueueItem &item, std::optional<double> pausedAt)
{
    m_inInternalSync = true;
    discardPreload(false);
    m_currentUid = item.uid;

    if (m_currentSource != item.source) {
        m_currentSource = item.source;
        emit currentSourceChanged(m_currentSource);
    }

    if (m_mpv.isValid()) {
        // 让新建的滤镜链一开始就带当前增益，缩小重建后到 audioReconfigured 之间的全音量窗口
        m_mpv.setProperty(QStringLiteral("af"), m_ducker.formattedDuckFilter(m_ducker.duckGain()));
        if (pausedAt.has_value()) {
            m_pause = true;
            m_mpv.setProperty(QStringLiteral("pause"), true);
            const double pos
                = (std::isnan(*pausedAt) || !std::isfinite(*pausedAt) || *pausedAt < 0.0)
                ? 0.0
                : *pausedAt;
            const QString options = QStringLiteral("pause=yes,start=%1").arg(pos, 0, 'f', 6);
            m_mpv.command({ QStringLiteral("loadfile"), item.source, QStringLiteral("replace"),
                QStringLiteral("0"), options });
        } else {
            m_mpv.command({ QStringLiteral("loadfile"), item.source, QStringLiteral("replace") });
            m_mpv.setProperty(QStringLiteral("pause"), false);
        }
        m_currentEntryId = lastPlaylistEntryId();
        if (m_currentEntryId != -1) {
            m_entrySources.insert(m_currentEntryId, item.source);
        }
    }
    m_inInternalSync = false;

    schedulePreloadSync();
}

void Player::finishPlayback(bool emitFinished)
{
    m_currentEntryId = -1;
    m_currentUid = 0;
    stop();
    if (emitFinished) {
        emit playbackFinished();
    }
}

qint64 Player::lastPlaylistEntryId() const
{
    if (!m_mpv.isValid()) {
        return -1;
    }
    const QVariantList list = m_mpv.property(QStringLiteral("playlist")).toList();
    if (!list.isEmpty()) {
        return list.last().toMap().value(QStringLiteral("id")).toLongLong();
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
        const auto advItem = m_queue.advance(PlayOrder::Advance::Auto);
        if (!advItem.has_value() || advItem->uid != m_preloadUid) {
            qCWarning(lcPlayer) << "Natural transition mismatch: expected preload uid"
                                << m_preloadUid << "got" << (advItem ? advItem->uid : 0);
            if (advItem.has_value()) {
                loadItem(*advItem);
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

        if (m_mpv.isValid()) {
            m_mpv.command({ QStringLiteral("playlist-remove"), QStringLiteral("0") });
        }

        schedulePreloadSync();
    }
}

void Player::onFileLoaded()
{
    qCDebug(lcPlayer) << "fileLoaded for:" << m_currentSource;
    m_consecutiveErrorCount = 0;
    schedulePreloadSync();
}

void Player::onEndFile(qint64 entryId, MpvHandle::EndFileReason reason, const QString &error)
{
    qCDebug(lcPlayer) << "endFile:" << entryId << "reason:" << static_cast<int>(reason)
                      << "error:" << error;
    if (m_ignoredEntryIds.remove(entryId)) {
        qCDebug(lcPlayer) << "Ignoring endFile for discarded entry:" << entryId;
        m_entrySources.remove(entryId);
        return;
    }

    const QString failedSource = m_entrySources.value(entryId, m_currentSource);
    m_entrySources.remove(entryId);

    if (reason == MpvHandle::EndFileReason::Eof) {
        if (m_preloadEntryId == -1 && (entryId == m_currentEntryId || m_currentEntryId == -1)) {
            const auto nextItem = m_queue.advance(PlayOrder::Advance::Auto);
            if (nextItem.has_value()) {
                loadItem(*nextItem);
            } else {
                finishPlayback();
            }
        }
    } else if (reason == MpvHandle::EndFileReason::Error) {
        handleEndFileError(entryId, failedSource, error);
    }
}

void Player::handleEndFileError(qint64 entryId, const QString &failedSource, const QString &error)
{
    qCWarning(lcPlayer) << "Playback error on" << failedSource << ":" << error;
    emit playbackError(failedSource, error);

    ++m_consecutiveErrorCount;
    if (m_consecutiveErrorCount >= std::max(1, m_queue.count())) {
        qCWarning(lcPlayer) << "all items failed (" << m_consecutiveErrorCount
                            << "consecutive errors), stopping playback";
        finishPlayback(m_queue.mode() == PlayMode::Sequential);
        return;
    }

    discardPreload(true);

    if (entryId == m_currentEntryId || m_currentEntryId == -1 || m_idleActive) {
        const auto nextItem = m_queue.advance(PlayOrder::Advance::User);
        if (nextItem.has_value()) {
            loadItem(*nextItem);
        } else {
            finishPlayback();
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

    if (m_inInternalSync || !m_mpv.isValid()) {
        return;
    }

    if (m_currentEntryId == -1) {
        discardPreload(true);
        return;
    }

    const auto targetNext = m_queue.peekNext(PlayOrder::Advance::Auto);
    const bool isRepeatOne = (m_queue.mode() == PlayMode::RepeatOne);

    if (!targetNext.has_value()) {
        discardPreload(true);
        return;
    }

    const QueueItem &nextItem = *targetNext;

    if (m_preloadEntryId != -1 && m_preloadUid == nextItem.uid
        && m_preloadIsRepeatOne == isRepeatOne) {
        return;
    }

    m_inInternalSync = true;
    discardPreload(true);

    m_mpv.command({ QStringLiteral("loadfile"), nextItem.source, QStringLiteral("append") });
    m_preloadEntryId = lastPlaylistEntryId();
    m_preloadUid = nextItem.uid;
    m_preloadIsRepeatOne = isRepeatOne;
    if (m_preloadEntryId != -1) {
        m_entrySources.insert(m_preloadEntryId, nextItem.source);
    }
    m_inInternalSync = false;
}

} // namespace linernotes::player
