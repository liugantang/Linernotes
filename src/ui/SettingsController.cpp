// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SettingsController.h"

#include "AppSettings.h"
#include "UiLogging.h"

#include <core/Settings.h>
#include <player/Player.h>

namespace linernotes::ui {

namespace {

QString themeModeToString(SettingsController::ThemeMode mode)
{
    switch (mode) {
    case SettingsController::ThemeMode::System:
        return QStringLiteral("system");
    case SettingsController::ThemeMode::Light:
        return QStringLiteral("light");
    case SettingsController::ThemeMode::Dark:
        return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

SettingsController::ThemeMode themeModeFromString(const QString &str)
{
    if (str == QStringLiteral("light")) {
        return SettingsController::ThemeMode::Light;
    }
    if (str == QStringLiteral("dark")) {
        return SettingsController::ThemeMode::Dark;
    }
    return SettingsController::ThemeMode::System;
}

QString replayGainModeToString(player::Player::ReplayGainMode mode)
{
    switch (mode) {
    case player::Player::ReplayGainMode::Off:
        return QStringLiteral("off");
    case player::Player::ReplayGainMode::Track:
        return QStringLiteral("track");
    case player::Player::ReplayGainMode::Album:
        return QStringLiteral("album");
    }
    return QStringLiteral("track");
}

player::Player::ReplayGainMode replayGainModeFromString(const QString &str)
{
    if (str == QStringLiteral("off")) {
        return player::Player::ReplayGainMode::Off;
    }
    if (str == QStringLiteral("album")) {
        return player::Player::ReplayGainMode::Album;
    }
    return player::Player::ReplayGainMode::Track;
}

} // namespace

SettingsController::SettingsController(
    core::Settings &settings, player::Player &player, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_player(player)
    , m_replayGainMode(replayGainModeFromString(m_settings.value(kPlaybackReplayGain)))
    , m_gapless(m_settings.value(kPlaybackGapless))
    , m_audioDevice(m_settings.value(kPlaybackAudioDevice))
    , m_exclusiveMode(m_settings.value(kPlaybackExclusive))
    , m_themeMode(themeModeFromString(m_settings.value(kAppearanceTheme)))
    , m_accentFromCover(m_settings.value(kAppearanceAccentFromCover))
{
    m_player.setReplayGainMode(m_replayGainMode);
    m_player.setGapless(m_gapless);
    if (!m_audioDevice.isEmpty()) {
        m_player.refreshAudioDevices();
        if (!m_player.selectAudioDevice(m_audioDevice)) {
            qCWarning(lcUi) << "Failed to select configured audio device:" << m_audioDevice;
        }
    }
    m_player.setExclusiveMode(m_exclusiveMode);
}

player::Player::ReplayGainMode SettingsController::replayGainMode() const
{
    return m_replayGainMode;
}

void SettingsController::setReplayGainMode(player::Player::ReplayGainMode mode)
{
    if (mode == m_replayGainMode) {
        return;
    }
    m_replayGainMode = mode;
    m_player.setReplayGainMode(mode);
    m_settings.setValue(kPlaybackReplayGain, replayGainModeToString(mode));
    emit replayGainModeChanged();
}

bool SettingsController::gapless() const
{
    return m_gapless;
}

void SettingsController::setGapless(bool gapless)
{
    if (gapless == m_gapless) {
        return;
    }
    m_gapless = gapless;
    m_player.setGapless(gapless);
    m_settings.setValue(kPlaybackGapless, gapless);
    emit gaplessChanged();
}

QString SettingsController::audioDevice() const
{
    return m_audioDevice;
}

void SettingsController::setAudioDevice(const QString &device)
{
    if (device == m_audioDevice) {
        return;
    }
    m_audioDevice = device;
    if (device.isEmpty()) {
        m_player.selectAudioDevice(QStringLiteral("auto"));
    } else {
        m_player.refreshAudioDevices();
        if (!m_player.selectAudioDevice(device)) {
            qCWarning(lcUi) << "Failed to select audio device:" << device;
        }
    }
    m_settings.setValue(kPlaybackAudioDevice, device);
    emit audioDeviceChanged();
}

bool SettingsController::exclusiveMode() const
{
    return m_exclusiveMode;
}

void SettingsController::setExclusiveMode(bool exclusive)
{
    if (exclusive == m_exclusiveMode) {
        return;
    }
    m_exclusiveMode = exclusive;
    m_player.setExclusiveMode(exclusive);
    m_settings.setValue(kPlaybackExclusive, exclusive);
    emit exclusiveModeChanged();
}

SettingsController::ThemeMode SettingsController::themeMode() const
{
    return m_themeMode;
}

void SettingsController::setThemeMode(ThemeMode mode)
{
    if (mode == m_themeMode) {
        return;
    }
    m_themeMode = mode;
    m_settings.setValue(kAppearanceTheme, themeModeToString(mode));
    emit themeModeChanged();
}

bool SettingsController::accentFromCover() const
{
    return m_accentFromCover;
}

void SettingsController::setAccentFromCover(bool enabled)
{
    if (enabled == m_accentFromCover) {
        return;
    }
    m_accentFromCover = enabled;
    m_settings.setValue(kAppearanceAccentFromCover, enabled);
    emit accentFromCoverChanged();
}

} // namespace linernotes::ui
