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

bool parseBoolVariant(const QVariant &value)
{
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    if (value.metaType().id() == QMetaType::QString) {
        const QString s = value.toString();
        return (s.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0
            || s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
            || s == QLatin1String("1"));
    }
    if (value.canConvert<qint64>()) {
        return value.toLongLong() != 0;
    }
    return value.toBool();
}

QVariantList parseAudioDeviceList(const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return { };
    }
    const QVariantList rawList = value.toList();
    QVariantList devices;
    devices.reserve(rawList.size());
    for (const QVariant &itemVar : rawList) {
        const QVariantMap itemMap = itemVar.toMap();
        const QString name = itemMap.value(QStringLiteral("name")).toString();
        QString description = itemMap.value(QStringLiteral("description")).toString();
        if (description.isEmpty()) {
            description = name;
        }
        QVariantMap dev;
        dev.insert(QStringLiteral("name"), name);
        dev.insert(QStringLiteral("description"), description);
        devices.append(dev);
    }
    return devices;
}
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
    m_mpv->observeProperty(QStringLiteral("audio-device-list"));
    m_mpv->observeProperty(QStringLiteral("audio-device"));
    m_mpv->observeProperty(QStringLiteral("audio-exclusive"));

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
    const QVariant devListVar = m_mpv->property(QStringLiteral("audio-device-list"));
    if (devListVar.isValid()) {
        m_audioDevices = parseAudioDeviceList(devListVar);
    }
    const QVariant devVar = m_mpv->property(QStringLiteral("audio-device"));
    if (devVar.isValid() && !devVar.isNull()) {
        const QString devName = devVar.toString();
        if (!devName.isEmpty()) {
            m_audioDevice = devName;
        }
    }
    const QVariant exclVar = m_mpv->property(QStringLiteral("audio-exclusive"));
    if (exclVar.isValid() && !exclVar.isNull()) {
        m_exclusiveMode = parseBoolVariant(exclVar);
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
    connect(m_mpv, &MpvHandle::fileLoaded, this, &Player::onFileLoaded);
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

QVariantList Player::audioDevices() const
{
    return m_audioDevices;
}

QString Player::audioDevice() const
{
    return m_audioDevice;
}

bool Player::exclusiveMode() const
{
    return m_exclusiveMode;
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
    m_consecutiveErrorCount = 0;
    m_queue->setItems({ { .source = path } }, -1);
    playIndex(0);
}

void Player::playIndex(int row)
{
    qCDebug(lcPlayer) << "playIndex:" << row;
    m_consecutiveErrorCount = 0;
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
    m_consecutiveErrorCount = 0;
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
    m_consecutiveErrorCount = 0;
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
    m_consecutiveErrorCount = 0;
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
    if (m_currentEntryId != -1) {
        m_ignoredEntryIds.insert(m_currentEntryId);
    }
    if (m_preloadEntryId != -1) {
        m_ignoredEntryIds.insert(m_preloadEntryId);
    }
    m_preloadEntryId = -1;
    m_preloadUid = 0;
    m_preloadIsRepeatOne = false;
    m_currentEntryId = -1;
    m_currentUid = 0;
    m_entrySources.clear();
    m_consecutiveErrorCount = 0;

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

bool Player::selectAudioDevice(const QString &name)
{
    bool found = false;
    for (const QVariant &devVar : m_audioDevices) {
        if (devVar.toMap().value(QStringLiteral("name")).toString() == name) {
            found = true;
            break;
        }
    }

    if (!found) {
        qCWarning(lcPlayer) << "Cannot select audio device" << name
                            << "because it is not in audioDevices list";
        return false;
    }

    qCDebug(lcPlayer) << "selectAudioDevice() to" << name;
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return false;
    }

    return m_mpv->setProperty(QStringLiteral("audio-device"), name);
}

void Player::setExclusiveMode(bool exclusive)
{
    if (exclusive == m_exclusiveMode) {
        return;
    }
    qCDebug(lcPlayer) << "setExclusiveMode() to" << exclusive;
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->setProperty(QStringLiteral("audio-exclusive"), exclusive);
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

void Player::handleAudioDeviceListChanged(const QVariant &value)
{
    const QVariantList devices = parseAudioDeviceList(value);
    if (devices != m_audioDevices) {
        m_audioDevices = devices;
        qCDebug(lcPlayer) << "Audio device list changed, count:" << m_audioDevices.size();

        bool containsCurrent = false;
        for (const QVariant &devVar : m_audioDevices) {
            if (devVar.toMap().value(QStringLiteral("name")).toString() == m_audioDevice) {
                containsCurrent = true;
                break;
            }
        }
        if (!containsCurrent && !m_audioDevice.isEmpty()) {
            qCInfo(lcPlayer) << "Selected audio device" << m_audioDevice
                             << "is no longer present in audioDevices list";
        }

        emit audioDevicesChanged();
    }
}

void Player::handleAudioDeviceChanged(const QVariant &value)
{
    if (value.isValid() && !value.isNull()) {
        const QString name = value.toString();
        if (!name.isEmpty() && name != m_audioDevice) {
            m_audioDevice = name;
            qCDebug(lcPlayer) << "Audio device changed to:" << m_audioDevice;
            emit audioDeviceChanged(m_audioDevice);
        }
    }
}

void Player::handleAudioExclusiveChanged(const QVariant &value)
{
    if (value.isValid() && !value.isNull()) {
        const bool exclusive = parseBoolVariant(value);
        if (exclusive != m_exclusiveMode) {
            m_exclusiveMode = exclusive;
            qCDebug(lcPlayer) << "Exclusive mode changed to:" << m_exclusiveMode;
            emit exclusiveModeChanged(m_exclusiveMode);
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
    } else if (name == QStringLiteral("audio-device-list")) {
        handleAudioDeviceListChanged(value);
    } else if (name == QStringLiteral("audio-device")) {
        handleAudioDeviceChanged(value);
    } else if (name == QStringLiteral("audio-exclusive")) {
        handleAudioExclusiveChanged(value);
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
    if (m_preloadEntryId != -1) {
        m_ignoredEntryIds.insert(m_preloadEntryId);
        m_entrySources.remove(m_preloadEntryId);
    }
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
        if (m_currentEntryId != -1) {
            m_entrySources.insert(m_currentEntryId, item.source);
        }
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
            m_queue->advance(PlayOrder::Advance::Auto);
            m_currentEntryId = -1;
            m_currentUid = 0;
            emit playbackFinished();
        }
    } else if (reason == MpvHandle::EndFileReason::Error) {
        qCWarning(lcPlayer) << "Playback error on" << failedSource << ":" << error;
        emit playbackError(failedSource, error);

        ++m_consecutiveErrorCount;
        if (m_consecutiveErrorCount >= std::max(1, m_queue->count())) {
            qCWarning(lcPlayer) << "all items failed (" << m_consecutiveErrorCount
                                << "consecutive errors), stopping playback";
            m_currentEntryId = -1;
            m_currentUid = 0;
            stop();
            if (m_queue->mode() == PlayMode::Sequential) {
                emit playbackFinished();
            }
            return;
        }

        if (m_preloadIsRepeatOne && m_preloadEntryId != -1) {
            m_inInternalSync = true;
            m_ignoredEntryIds.insert(m_preloadEntryId);
            m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
            m_entrySources.remove(m_preloadEntryId);
            m_preloadEntryId = -1;
            m_preloadUid = 0;
            m_preloadIsRepeatOne = false;
            m_inInternalSync = false;
        }

        if (entryId == m_currentEntryId || m_currentEntryId == -1) {
            if (m_preloadEntryId != -1) {
                // mpv has preloaded next item and will automatically transition to it.
            } else {
                const auto nextItem = m_queue->advance(PlayOrder::Advance::User);
                if (nextItem.has_value()) {
                    loadCurrentItem(*nextItem);
                } else {
                    m_currentEntryId = -1;
                    m_currentUid = 0;
                    stop();
                    emit playbackFinished();
                }
            }
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

    if (m_currentEntryId == -1) {
        if (m_preloadEntryId != -1) {
            m_inInternalSync = true;
            m_ignoredEntryIds.insert(m_preloadEntryId);
            m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
            m_entrySources.remove(m_preloadEntryId);
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
            m_ignoredEntryIds.insert(m_preloadEntryId);
            m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
            m_entrySources.remove(m_preloadEntryId);
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
        m_ignoredEntryIds.insert(m_preloadEntryId);
        m_mpv->command({ QStringLiteral("playlist-remove"), QStringLiteral("1") });
        m_entrySources.remove(m_preloadEntryId);
        m_preloadEntryId = -1;
        m_preloadUid = 0;
        m_preloadIsRepeatOne = false;
    }

    m_mpv->command({ QStringLiteral("loadfile"), nextItem.source, QStringLiteral("append") });
    m_preloadEntryId = lastPlaylistEntryId();
    m_preloadUid = nextItem.uid;
    m_preloadIsRepeatOne = isRepeatOne;
    if (m_preloadEntryId != -1) {
        m_entrySources.insert(m_preloadEntryId, nextItem.source);
    }
    m_inInternalSync = false;
}

} // namespace linernotes::player
