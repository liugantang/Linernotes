// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Player.h"

#include "PlayerLogging.h"

#include <algorithm>
#include <cmath>

namespace linernotes::player {

Player::Player(const MpvHandle::OptionList &extraOptions, QObject *parent)
    : QObject(parent)
    , m_mpv(new MpvHandle(extraOptions, this))
{
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
    connect(m_mpv, &MpvHandle::endFile, this,
        [this](qint64 /*entryId*/, MpvHandle::EndFileReason reason, const QString & /*error*/) {
            if (reason == MpvHandle::EndFileReason::Eof) {
                emit playbackFinished();
            }
        });
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

void Player::openFile(const QString &path)
{
    qCDebug(lcPlayer) << "Opening file:" << path;
    if (m_currentSource != path) {
        m_currentSource = path;
        emit currentSourceChanged(m_currentSource);
    }
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") });
    m_mpv->setProperty(QStringLiteral("pause"), false);
}

void Player::play()
{
    qCDebug(lcPlayer) << "play() called, current state:" << static_cast<int>(m_state)
                      << "currentSource:" << m_currentSource;
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }

    if (m_state == PlaybackState::Paused) {
        m_mpv->setProperty(QStringLiteral("pause"), false);
    } else if (m_state == PlaybackState::Stopped && !m_currentSource.isEmpty()) {
        m_mpv->command({ QStringLiteral("loadfile"), m_currentSource, QStringLiteral("replace") });
        m_mpv->setProperty(QStringLiteral("pause"), false);
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
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        return;
    }
    m_mpv->command({ QStringLiteral("stop") });
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

} // namespace linernotes::player
