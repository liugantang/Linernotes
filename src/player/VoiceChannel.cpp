// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "VoiceChannel.h"

#include "PlayerLogging.h"

#include <algorithm>
#include <cmath>

namespace linernotes::player {

namespace {
MpvHandle::OptionList buildVoiceOptions(const MpvHandle::OptionList &extraOptions)
{
    MpvHandle::OptionList options = {
        { QStringLiteral("gapless-audio"), QStringLiteral("no") },
        { QStringLiteral("replaygain"), QStringLiteral("no") },
        { QStringLiteral("audio-client-name"), QStringLiteral("linernotes-voice") },
    };

    for (const auto &extra : extraOptions) {
        bool replaced = false;
        for (auto &opt : options) {
            if (opt.first == extra.first) {
                opt.second = extra.second;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            options.append(extra);
        }
    }
    return options;
}
} // namespace

VoiceChannel::VoiceChannel(const MpvHandle::OptionList &extraOptions, QObject *parent)
    : QObject(parent)
    , m_mpv(new MpvHandle(buildVoiceOptions(extraOptions), this))
{
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        qCWarning(lcPlayer) << "VoiceChannel initialization failed because MpvHandle is invalid:"
                            << ((m_mpv != nullptr) ? m_mpv->errorString()
                                                   : QStringLiteral("null handle"));
        return;
    }

    m_mpv->observeProperty(QStringLiteral("volume"));

    const QVariant volVar = m_mpv->property(QStringLiteral("volume"));
    if (volVar.isValid()) {
        m_volume = std::clamp(static_cast<int>(std::round(volVar.toDouble())), 0, 100);
    }

    connect(m_mpv, &MpvHandle::propertyChanged, this, &VoiceChannel::onPropertyChanged);
    connect(m_mpv, &MpvHandle::startFile, this, &VoiceChannel::onStartFile);
    connect(m_mpv, &MpvHandle::fileLoaded, this, &VoiceChannel::onFileLoaded);
    connect(m_mpv, &MpvHandle::endFile, this, &VoiceChannel::onEndFile);
}

bool VoiceChannel::isValid() const
{
    return m_mpv != nullptr && m_mpv->isValid();
}

bool VoiceChannel::isPlaying() const
{
    return m_playing;
}

int VoiceChannel::volume() const
{
    return m_volume;
}

void VoiceChannel::play(const QString &path)
{
    qCDebug(lcPlayer) << "VoiceChannel::play:" << path;
    if (m_mpv == nullptr || !m_mpv->isValid()) {
        emit failed(QStringLiteral("MpvHandle is invalid"));
        return;
    }

    if (path.isEmpty()) {
        stop();
        return;
    }

    m_userStopped = false;
    m_currentEntryId = -1;
    m_mpv->command({ QStringLiteral("loadfile"), path, QStringLiteral("replace") });
    m_mpv->setProperty(QStringLiteral("pause"), false);
}

void VoiceChannel::stop()
{
    qCDebug(lcPlayer) << "VoiceChannel::stop";
    m_userStopped = true;
    m_currentEntryId = -1;

    if (m_mpv != nullptr && m_mpv->isValid()) {
        m_mpv->command({ QStringLiteral("stop") });
    }

    if (m_playing) {
        m_playing = false;
        emit playingChanged(false);
    }
}

void VoiceChannel::setVolume(int volume)
{
    const int clamped = std::clamp(volume, 0, 100);
    if (clamped == m_volume) {
        return;
    }
    qCDebug(lcPlayer) << "VoiceChannel::setVolume to" << clamped;
    m_volume = clamped;
    emit volumeChanged(m_volume);

    if (m_mpv != nullptr && m_mpv->isValid()) {
        m_mpv->setProperty(QStringLiteral("volume"), static_cast<double>(clamped));
    }
}

void VoiceChannel::onPropertyChanged(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("volume")) {
        if (value.isValid() && !value.isNull()) {
            const int vol = std::clamp(static_cast<int>(std::round(value.toDouble())), 0, 100);
            if (vol != m_volume) {
                m_volume = vol;
                emit volumeChanged(m_volume);
            }
        }
    }
}

void VoiceChannel::onStartFile(qint64 entryId)
{
    qCDebug(lcPlayer) << "VoiceChannel::onStartFile entryId:" << entryId;
    m_currentEntryId = entryId;
    m_userStopped = false;
}

void VoiceChannel::onFileLoaded()
{
    qCDebug(lcPlayer) << "VoiceChannel::onFileLoaded";
    if (m_userStopped) {
        return;
    }
    if (!m_playing) {
        m_playing = true;
        emit playingChanged(true);
    }
    emit started();
}

void VoiceChannel::onEndFile(qint64 entryId, MpvHandle::EndFileReason reason, const QString &error)
{
    qCDebug(lcPlayer) << "VoiceChannel::onEndFile entryId:" << entryId
                      << "reason:" << static_cast<int>(reason) << "error:" << error;
    if (m_currentEntryId != -1 && entryId != -1 && entryId != m_currentEntryId) {
        qCDebug(lcPlayer) << "VoiceChannel ignoring endFile for older entry:" << entryId;
        return;
    }
    m_currentEntryId = -1;

    if (m_userStopped) {
        return;
    }

    if (reason == MpvHandle::EndFileReason::Eof) {
        if (m_playing) {
            m_playing = false;
            emit playingChanged(false);
        }
        emit finished();
    } else if (reason == MpvHandle::EndFileReason::Error) {
        if (m_playing) {
            m_playing = false;
            emit playingChanged(false);
        }
        emit failed(error.isEmpty() ? QStringLiteral("Playback error") : error);
    } else if (reason == MpvHandle::EndFileReason::Stop
        || reason == MpvHandle::EndFileReason::Quit) {
        if (m_playing) {
            m_playing = false;
            emit playingChanged(false);
        }
    }
}

} // namespace linernotes::player
