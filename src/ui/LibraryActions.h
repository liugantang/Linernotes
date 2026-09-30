// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <core/PlaySource.h>

#include <cstdint>

namespace linernotes::core {
class Settings;
} // namespace linernotes::core

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
    LibraryActions(library::Database &db, player::Player &player, core::Settings &settings,
        QObject *parent = nullptr);
    ~LibraryActions() override = default;

    /// 用这些曲目替换播放队列，从 startIndex 开始播放
    Q_INVOKABLE void playTracks(
        const QList<qint64> &trackIds, int startIndex, linernotes::core::PlaySource source);
    Q_INVOKABLE void playNext(const QList<qint64> &trackIds); // 插到当前曲目之后
    Q_INVOKABLE void enqueue(const QList<qint64> &trackIds); // 加到队尾

    /// 打开指定的音频文件并从第一项开始播放。
    /// 路径转为绝对路径，跳过不存在或目录；库内文件附带 trackId，库外文件 trackId 为 -1。
    Q_INVOKABLE void openFiles(const QStringList &paths);

    /// 专辑/艺人详情页头部信息；不存在返回空 map。
    /// 键：albumId,title,albumArtist,year,trackCount,durationText,coverHash /
    /// artistId,name,originalName,trackCount,albumCount,coverHash
    [[nodiscard]] Q_INVOKABLE QVariantMap albumInfo(qint64 albumId) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap artistInfo(qint64 artistId) const;

    /// 同一作品的其他版本。每项：trackId、title、artist、album、durationText、versionType（int，同上，-1
    /// 表示未知）。
    [[nodiscard]] Q_INVOKABLE QVariantList otherVersions(qint64 trackId) const;

    /// 在系统文件管理器中打开该曲目所在目录。
    Q_INVOKABLE void showInFileManager(qint64 trackId) const;

private:
    library::Database &m_db;
    player::Player &m_player;
    core::Settings &m_settings;
};

} // namespace linernotes::ui
