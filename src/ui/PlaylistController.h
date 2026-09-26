// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <library/PlaylistStore.h>
#include <ui/PlaylistListModel.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class PlaylistController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PlaylistController)

    Q_PROPERTY(linernotes::ui::PlaylistListModel *model READ model CONSTANT)

public:
    PlaylistController(library::Database &db, player::Player &player);
    ~PlaylistController() override = default;

    [[nodiscard]] PlaylistListModel *model();

    Q_INVOKABLE void refresh();
    Q_INVOKABLE qint64 createManual(const QString &name, const QList<qint64> &trackIds = { });
    Q_INVOKABLE qint64 saveQueue(const QString &name);
    Q_INVOKABLE bool rename(qint64 id, const QString &name);
    Q_INVOKABLE bool remove(qint64 id);
    Q_INVOKABLE int addTracks(qint64 id, const QList<qint64> &trackIds);
    Q_INVOKABLE bool removeTracks(qint64 id, const QList<qint64> &trackIds);
    [[nodiscard]] std::optional<library::PlaylistInfo> info(qint64 id) const;
    [[nodiscard]] Q_INVOKABLE bool isManual(qint64 id) const;

signals:
    void playlistsChanged();
    void playlistContentChanged(qint64 id);

private:
    library::PlaylistStore m_store;
    player::Player &m_player;
    PlaylistListModel m_model;
};

} // namespace linernotes::ui
