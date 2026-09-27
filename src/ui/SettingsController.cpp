// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SettingsController.h"

#include "AppSettings.h"
#include "UiLogging.h"

#include <core/Settings.h>
#include <player/Player.h>

namespace linernotes::ui {

namespace {

QString languageToString(SettingsController::Language lang)
{
    switch (lang) {
    case SettingsController::Language::System:
        return QStringLiteral("system");
    case SettingsController::Language::English:
        return QStringLiteral("en");
    case SettingsController::Language::Chinese:
        return QStringLiteral("zh_CN");
    }
    return QStringLiteral("system");
}

SettingsController::Language languageFromString(const QString &str)
{
    if (str == QStringLiteral("en")) {
        return SettingsController::Language::English;
    }
    if (str == QStringLiteral("zh_CN") || str == QStringLiteral("zh")) {
        return SettingsController::Language::Chinese;
    }
    return SettingsController::Language::System;
}

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
    , m_countMinPercent(
          library::PlayCountRule { .minPercent = m_settings.value(kPlaybackCountMinPercent) }
              .normalized()
              .minPercent)
    , m_countMinSeconds(
          library::PlayCountRule { .minSeconds = m_settings.value(kPlaybackCountMinSeconds) }
              .normalized()
              .minSeconds)
    , m_language(languageFromString(m_settings.value(kAppearanceLanguage)))
    , m_themeMode(themeModeFromString(m_settings.value(kAppearanceTheme)))
    , m_accentFromCover(m_settings.value(kAppearanceAccentFromCover))
    , m_trayIcon(m_settings.value(kAppearanceTrayIcon))
    , m_closeToTray(m_settings.value(kAppearanceCloseToTray))
    , m_trackChangeNotifications(m_settings.value(kNotificationsTrackChange))
    , m_firstRunCompleted(m_settings.value(kAppFirstRunCompleted))
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

int SettingsController::countMinPercent() const
{
    return m_countMinPercent;
}

void SettingsController::setCountMinPercent(int percent)
{
    const int clamped = library::PlayCountRule { .minPercent = percent }.normalized().minPercent;
    if (clamped == m_countMinPercent) {
        return;
    }
    m_countMinPercent = clamped;
    m_settings.setValue(kPlaybackCountMinPercent, clamped);
    emit countMinPercentChanged();
    emit playCountRuleChanged();
}

int SettingsController::countMinSeconds() const
{
    return m_countMinSeconds;
}

void SettingsController::setCountMinSeconds(int seconds)
{
    const int clamped = library::PlayCountRule { .minSeconds = seconds }.normalized().minSeconds;
    if (clamped == m_countMinSeconds) {
        return;
    }
    m_countMinSeconds = clamped;
    m_settings.setValue(kPlaybackCountMinSeconds, clamped);
    emit countMinSecondsChanged();
    emit playCountRuleChanged();
}

library::PlayCountRule SettingsController::playCountRule() const
{
    return library::PlayCountRule {
        .minPercent = m_countMinPercent,
        .minSeconds = m_countMinSeconds,
    };
}

SettingsController::Language SettingsController::language() const
{
    return m_language;
}

void SettingsController::setLanguage(Language lang)
{
    if (lang == m_language) {
        return;
    }
    m_language = lang;
    m_settings.setValue(kAppearanceLanguage, languageToString(lang));
    emit languageChanged();
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

bool SettingsController::trayIcon() const
{
    return m_trayIcon;
}

void SettingsController::setTrayIcon(bool enabled)
{
    if (enabled == m_trayIcon) {
        return;
    }
    m_trayIcon = enabled;
    m_settings.setValue(kAppearanceTrayIcon, enabled);
    emit trayIconChanged();
}

bool SettingsController::closeToTray() const
{
    return m_closeToTray;
}

void SettingsController::setCloseToTray(bool enabled)
{
    if (enabled == m_closeToTray) {
        return;
    }
    m_closeToTray = enabled;
    m_settings.setValue(kAppearanceCloseToTray, enabled);
    emit closeToTrayChanged();
}

bool SettingsController::trackChangeNotifications() const
{
    return m_trackChangeNotifications;
}

void SettingsController::setTrackChangeNotifications(bool enabled)
{
    if (enabled == m_trackChangeNotifications) {
        return;
    }
    m_trackChangeNotifications = enabled;
    m_settings.setValue(kNotificationsTrackChange, enabled);
    emit trackChangeNotificationsChanged();
}

bool SettingsController::firstRunCompleted() const
{
    return m_firstRunCompleted;
}

void SettingsController::setFirstRunCompleted(bool completed)
{
    if (completed == m_firstRunCompleted) {
        return;
    }
    m_firstRunCompleted = completed;
    m_settings.setValue(kAppFirstRunCompleted, completed);
    emit firstRunCompletedChanged();
}

} // namespace linernotes::ui
