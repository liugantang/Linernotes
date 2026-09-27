// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <cstdint>

namespace linernotes::library {
class CoverStore;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class NowPlaying;
class SettingsController;
class NotificationSink;

class TrackNotifier : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TrackNotifier)

public:
    TrackNotifier(NowPlaying &nowPlaying, player::Player &player, const library::CoverStore &covers,
        const SettingsController &settings, NotificationSink &sink, QObject *parent = nullptr);
    ~TrackNotifier() override = default;

private:
    void checkAndNotify();

    NowPlaying &m_nowPlaying;
    player::Player &m_player;
    const library::CoverStore &m_covers;
    const SettingsController &m_settings;
    NotificationSink &m_sink;

    qint64 m_lastTrackId { -1 };
};

} // namespace linernotes::ui
