// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/PlayEventStore.h>
#include <player/ListenSession.h>
#include <player/PlayQueue.h>
#include <player/Player.h>

#include <optional>

namespace linernotes::ui {

class PlayEventRecorder : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PlayEventRecorder)

public:
    PlayEventRecorder(player::Player &player, library::Database &db, const core::Clock &clock,
        QObject *parent = nullptr);
    ~PlayEventRecorder() override;

signals:
    void playEventFinished(qint64 trackId);

private slots:
    void onTrackStarted(const linernotes::player::QueueItem &item);
    void onStateChanged(linernotes::player::Player::PlaybackState state);
    void onPositionChanged(double position);
    void onDurationChanged(double duration);
    void onCheckpointTimer();

private:
    enum class Notify : bool { No, Yes };
    void endCurrentSession(player::ListenEnd end, Notify notify = Notify::Yes);
    void checkpoint();
    bool syncToStore(bool ended, player::ListenEnd end = player::ListenEnd::Stopped);

    player::Player &m_player;
    library::Database &m_db;
    const core::Clock &m_clock;
    library::PlayEventStore m_store;

    struct CurrentSession {
        player::QueueItem item;
        player::ListenSession session;
        QString device;
        QString snapshotJson;
        qint64 eventId = -1;
    };

    std::optional<CurrentSession> m_current;
    QTimer m_checkpointTimer;
};

} // namespace linernotes::ui
