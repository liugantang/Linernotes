// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <library/LibraryEnums.h>
#include <library/PlaylistStore.h>
#include <library/SmartRule.h>
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
    Q_INVOKABLE qint64 createSmart(const QString &name, const library::SmartRule &rule);
    Q_INVOKABLE qint64 saveQueue(const QString &name);
    Q_INVOKABLE bool rename(qint64 id, const QString &name);
    Q_INVOKABLE bool remove(qint64 id);
    Q_INVOKABLE int addTracks(qint64 id, const QList<qint64> &trackIds);
    Q_INVOKABLE bool removeTracks(qint64 id, const QList<qint64> &trackIds);
    Q_INVOKABLE bool moveTracks(qint64 id, const QList<qint64> &trackIds, int beforePosition);
    Q_INVOKABLE bool setRule(qint64 id, const library::SmartRule &rule);
    [[nodiscard]] Q_INVOKABLE library::SmartRule rule(qint64 id) const;
    [[nodiscard]] Q_INVOKABLE QVariantList smartFields() const;
    [[nodiscard]] Q_INVOKABLE QVariantList smartOps(library::SmartField field) const;
    [[nodiscard]] Q_INVOKABLE library::SmartFieldKind smartFieldKind(
        library::SmartField field) const;
    [[nodiscard]] Q_INVOKABLE QVariantList smartSortKeys() const;
    [[nodiscard]] Q_INVOKABLE QString fieldLabel(library::SmartField field) const;
    [[nodiscard]] Q_INVOKABLE QString opLabel(library::SmartOp op) const;
    [[nodiscard]] Q_INVOKABLE QString sortKeyLabel(library::TrackSortKey key) const;
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
