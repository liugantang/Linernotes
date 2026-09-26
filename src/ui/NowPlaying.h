// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class NowPlaying : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NowPlaying)

    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed)
    Q_PROPERTY(qint64 trackId READ trackId NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(qint64 albumId READ albumId NOTIFY changed)
    Q_PROPERTY(QString coverHash READ coverHash NOTIFY changed)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY changed)
    Q_PROPERTY(bool favorite READ favorite NOTIFY changed)
    Q_PROPERTY(int rating READ rating NOTIFY changed)

public:
    NowPlaying(library::Database &db, player::Player &player, QObject *parent = nullptr);
    ~NowPlaying() override = default;

    Q_INVOKABLE void refresh();

    [[nodiscard]] bool hasTrack() const;
    [[nodiscard]] qint64 trackId() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString artist() const;
    [[nodiscard]] QString album() const;
    [[nodiscard]] qint64 albumId() const;
    [[nodiscard]] QString coverHash() const;
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] bool favorite() const;
    [[nodiscard]] int rating() const;

signals:
    void changed();

private:
    library::Database &m_db;
    player::Player &m_player;

    bool m_hasTrack { false };
    qint64 m_trackId { -1 };
    QString m_title;
    QString m_artist;
    QString m_album;
    qint64 m_albumId { 0 };
    QString m_coverHash;
    double m_durationSeconds { 0.0 };
    bool m_favorite { false };
    int m_rating { 0 };
};

} // namespace linernotes::ui
