// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QIdentityProxyModel>
#include <QList>
#include <QString>

#include <cstdint>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class QueueModel : public QIdentityProxyModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(QueueModel)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        TitleRole = Qt::UserRole + 100,
        ArtistRole,
        DurationTextRole,
        CoverHashRole,
    };
    Q_ENUM(Role)

    explicit QueueModel(library::Database *db, player::Player &player, QObject *parent = nullptr);
    ~QueueModel() override = default;

    void setDatabase(library::Database *db);
    Q_INVOKABLE void refresh();

    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void playAt(int row);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void removeRows(const QList<int> &rows);
    Q_INVOKABLE void clear();
    [[nodiscard]] Q_INVOKABLE QList<qint64> trackIds() const;

private:
    struct CachedTrackInfo {
        QString title;
        QString artist;
        QString durationText;
        QString coverHash;
    };

    [[nodiscard]] const CachedTrackInfo &trackInfo(qint64 trackId, const QString &source) const;

    library::Database *m_db { nullptr };
    player::Player &m_player;
    mutable QHash<qint64, CachedTrackInfo> m_cache;
};

} // namespace linernotes::ui
