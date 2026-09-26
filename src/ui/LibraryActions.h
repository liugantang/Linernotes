// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QVariantMap>

#include <cstdint>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class LibraryActions : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LibraryActions)

public:
    LibraryActions(library::Database &db, player::Player &player, QObject *parent = nullptr);
    ~LibraryActions() override = default;

    /// 用这些曲目替换播放队列，从 startIndex 开始播放
    Q_INVOKABLE void playTracks(const QList<qint64> &trackIds, int startIndex = 0);
    Q_INVOKABLE void playNext(const QList<qint64> &trackIds); // 插到当前曲目之后
    Q_INVOKABLE void enqueue(const QList<qint64> &trackIds); // 加到队尾

    /// 专辑/艺人详情页头部信息；不存在返回空 map。
    /// 键：albumId,title,albumArtist,year,trackCount,durationText,coverHash /
    /// artistId,name,trackCount,albumCount,coverHash
    [[nodiscard]] Q_INVOKABLE QVariantMap albumInfo(qint64 albumId) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap artistInfo(qint64 artistId) const;

private:
    library::Database &m_db;
    player::Player &m_player;
};

} // namespace linernotes::ui
