// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include <player/PlayMode.h>

#include <cstdint>
#include <optional>

namespace linernotes::player {

struct PlaybackSnapshot {
    struct Item {
        QString source;
        qint64 trackId = -1;

        bool operator==(const Item &other) const = default;
    };

    QList<Item> items;
    int currentIndex = -1;
    double position = 0.0; // 秒
    PlayMode mode = PlayMode::Sequential;
    int volume = 100;
    bool muted = false;
    QString audioDevice = QStringLiteral("auto");

    bool operator==(const PlaybackSnapshot &other) const = default;

    [[nodiscard]] QJsonObject toJson() const; // 带 "version": 1
    /// 解析失败、版本不认识、字段类型不对时返回 nullopt；个别缺失字段用默认值；
    /// currentIndex 越界时置 -1，position 为负或非有限值时置 0，volume 夹到 [0,100]。
    static std::optional<PlaybackSnapshot> fromJson(const QJsonObject &json);
};

/// 读写快照文件。写入用 QSaveFile（原子替换），失败返回 false 并 qCWarning。
class PlaybackStateStore {
public:
    explicit PlaybackStateStore(QString filePath);

    [[nodiscard]] bool save(const PlaybackSnapshot &snapshot) const;
    [[nodiscard]] std::optional<PlaybackSnapshot>
    load() const; // 文件不存在 → nullopt（不告警）；损坏 → nullopt 并 qCWarning

private:
    QString m_filePath;
};

} // namespace linernotes::player
