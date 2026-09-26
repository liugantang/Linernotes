// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

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

    Q_PROPERTY(linernotes::player::Player::ReplayGainMode replayGainMode READ replayGainMode WRITE
            setReplayGainMode NOTIFY replayGainModeChanged)
    Q_PROPERTY(bool gapless READ gapless WRITE setGapless NOTIFY gaplessChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice WRITE setAudioDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(
        bool exclusiveMode READ exclusiveMode WRITE setExclusiveMode NOTIFY exclusiveModeChanged)
    Q_PROPERTY(ThemeMode themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool accentFromCover READ accentFromCover WRITE setAccentFromCover NOTIFY
            accentFromCoverChanged)
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

    [[nodiscard]] ThemeMode themeMode() const;
    void setThemeMode(ThemeMode mode);

    [[nodiscard]] bool accentFromCover() const;
    void setAccentFromCover(bool enabled);

    [[nodiscard]] bool firstRunCompleted() const;
    void setFirstRunCompleted(bool completed);

signals:
    void replayGainModeChanged();
    void gaplessChanged();
    void audioDeviceChanged();
    void exclusiveModeChanged();
    void themeModeChanged();
    void accentFromCoverChanged();
    void firstRunCompletedChanged();

private:
    core::Settings &m_settings;
    player::Player &m_player;

    player::Player::ReplayGainMode m_replayGainMode { player::Player::ReplayGainMode::Track };
    bool m_gapless { true };
    QString m_audioDevice;
    bool m_exclusiveMode { false };
    ThemeMode m_themeMode { ThemeMode::System };
    bool m_accentFromCover { false };
    bool m_firstRunCompleted { false };
};

} // namespace linernotes::ui
