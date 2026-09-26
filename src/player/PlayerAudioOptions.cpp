// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Player.h"
#include "PlayerLogging.h"

namespace linernotes::player {

QVariantList Player::audioDevices() const
{
    return m_audioOutput.audioDevices();
}

QString Player::audioDevice() const
{
    return m_audioOutput.audioDevice();
}

bool Player::exclusiveMode() const
{
    return m_audioOutput.exclusiveMode();
}

Player::ReplayGainMode Player::replayGainMode() const
{
    return m_replayGainMode;
}

bool Player::gapless() const
{
    return m_gapless;
}

void Player::refreshAudioDevices()
{
    m_audioOutput.refreshAudioDevices();
}

bool Player::selectAudioDevice(const QString &name)
{
    return m_audioOutput.selectAudioDevice(name);
}

void Player::setExclusiveMode(bool exclusive)
{
    m_audioOutput.setExclusiveMode(exclusive);
}

void Player::setReplayGainMode(ReplayGainMode mode)
{
    if (mode == m_replayGainMode) {
        return;
    }
    m_replayGainMode = mode;
    qCDebug(lcPlayer) << "setReplayGainMode() to" << static_cast<int>(mode);
    if (m_mpv.isValid()) {
        QString str;
        switch (mode) {
        case ReplayGainMode::Off:
            str = QStringLiteral("no");
            break;
        case ReplayGainMode::Track:
            str = QStringLiteral("track");
            break;
        case ReplayGainMode::Album:
            str = QStringLiteral("album");
            break;
        }
        m_mpv.setProperty(QStringLiteral("replaygain"), str);
    }
    emit replayGainModeChanged(m_replayGainMode);
}

void Player::setGapless(bool gapless)
{
    if (gapless == m_gapless) {
        return;
    }
    m_gapless = gapless;
    qCDebug(lcPlayer) << "setGapless() to" << gapless;
    if (m_mpv.isValid()) {
        m_mpv.setProperty(QStringLiteral("gapless-audio"),
            gapless ? QStringLiteral("weak") : QStringLiteral("no"));
    }
    emit gaplessChanged(m_gapless);
}

} // namespace linernotes::player
