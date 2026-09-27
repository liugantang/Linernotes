// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <library/PlayCountRule.h>
#include <player/Player.h>

#include <cstdint>

namespace linernotes::core {
class Settings;
} // namespace linernotes::core

namespace linernotes::ui {

class SettingsController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SettingsController)

public:
    enum class ThemeMode : std::uint8_t { System, Light, Dark };
    Q_ENUM(ThemeMode)

    enum class Language : std::uint8_t { System, English, Chinese };
    Q_ENUM(Language)

    Q_PROPERTY(linernotes::player::Player::ReplayGainMode replayGainMode READ replayGainMode WRITE
            setReplayGainMode NOTIFY replayGainModeChanged)
    Q_PROPERTY(bool gapless READ gapless WRITE setGapless NOTIFY gaplessChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(
        bool exclusiveMode READ exclusiveMode WRITE setExclusiveMode NOTIFY exclusiveModeChanged)
    Q_PROPERTY(int countMinPercent READ countMinPercent WRITE setCountMinPercent NOTIFY
            countMinPercentChanged)
    Q_PROPERTY(int countMinSeconds READ countMinSeconds WRITE setCountMinSeconds NOTIFY
            countMinSecondsChanged)
    Q_PROPERTY(Language language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(ThemeMode themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool accentFromCover READ accentFromCover WRITE setAccentFromCover NOTIFY
            accentFromCoverChanged)
    Q_PROPERTY(bool trayIcon READ trayIcon WRITE setTrayIcon NOTIFY trayIconChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
    Q_PROPERTY(bool trackChangeNotifications READ trackChangeNotifications WRITE
            setTrackChangeNotifications NOTIFY trackChangeNotificationsChanged)
    Q_PROPERTY(bool firstRunCompleted READ firstRunCompleted WRITE setFirstRunCompleted NOTIFY
            firstRunCompletedChanged)

    explicit SettingsController(
        core::Settings &settings, player::Player &player, QObject *parent = nullptr);
    ~SettingsController() override = default;

    [[nodiscard]] player::Player::ReplayGainMode replayGainMode() const;
    void setReplayGainMode(player::Player::ReplayGainMode mode);

    [[nodiscard]] bool gapless() const;
    void setGapless(bool gapless);

    [[nodiscard]] QString audioDevice() const;
    void setAudioDevice(const QString &device);

    [[nodiscard]] bool exclusiveMode() const;
    void setExclusiveMode(bool exclusive);

    [[nodiscard]] int countMinPercent() const;
    void setCountMinPercent(int percent);

    [[nodiscard]] int countMinSeconds() const;
    void setCountMinSeconds(int seconds);

    [[nodiscard]] library::PlayCountRule playCountRule() const;

    [[nodiscard]] Language language() const;
    void setLanguage(Language lang);

    [[nodiscard]] ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);

    [[nodiscard]] bool accentFromCover() const;
    void setAccentFromCover(bool enabled);

    [[nodiscard]] bool trayIcon() const;
    void setTrayIcon(bool enabled);

    [[nodiscard]] bool closeToTray() const;
    void setCloseToTray(bool enabled);

    [[nodiscard]] bool trackChangeNotifications() const;
    void setTrackChangeNotifications(bool enabled);

    [[nodiscard]] bool firstRunCompleted() const;
    void setFirstRunCompleted(bool completed);

signals:
    void replayGainModeChanged();
    void gaplessChanged();
    void audioDeviceChanged();
    void exclusiveModeChanged();
    void countMinPercentChanged();
    void countMinSecondsChanged();
    void playCountRuleChanged();
    void languageChanged();
    void themeModeChanged();
    void accentFromCoverChanged();
    void trayIconChanged();
    void closeToTrayChanged();
    void trackChangeNotificationsChanged();
    void firstRunCompletedChanged();

private:
    core::Settings &m_settings;
    player::Player &m_player;

    player::Player::ReplayGainMode m_replayGainMode { player::Player::ReplayGainMode::Track };
    bool m_gapless { true };
    QString m_audioDevice;
    bool m_exclusiveMode { false };
    int m_countMinPercent { 50 };
    int m_countMinSeconds { 240 };
    Language m_language { Language::System };
    ThemeMode m_themeMode { ThemeMode::System };
    bool m_accentFromCover { false };
    bool m_trayIcon { true };
    bool m_closeToTray { false };
    bool m_trackChangeNotifications { true };
    bool m_firstRunCompleted { false };
};

} // namespace linernotes::ui
