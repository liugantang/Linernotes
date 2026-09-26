// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <core/Result.h>
#include <library/SmartRule.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

class Database;

enum class PlaylistKind : std::uint8_t { Manual, Smart };

struct PlaylistInfo {
    qint64 id = 0;
    QString name;
    PlaylistKind kind = PlaylistKind::Manual;
    std::optional<SmartRule> rule; // 仅 Smart
    int position = 0;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    bool operator==(const PlaylistInfo &) const = default;
};

class PlaylistStore {
public:
    explicit PlaylistStore(Database &db);

    core::Result<QList<PlaylistInfo>> list() const; // 按 position 升序
    core::Result<std::optional<PlaylistInfo>> playlist(qint64 id) const;
    core::Result<qint64> createManual(const QString &name, const QList<qint64> &trackIds);
    core::Result<qint64> createSmart(const QString &name, const SmartRule &rule);
    core::Result<void> rename(qint64 id, const QString &name);
    core::Result<void> setRule(qint64 id, const SmartRule &rule); // 仅 Smart，否则 kPlaylistInvalid
    core::Result<void> remove(qint64 id); // 级联删 items
    /// 追加（或插入到 beforePosition 之前）；已在歌单中的曲目跳过；返回实际加入的数量。仅 Manual。
    core::Result<int> addTracks(
        qint64 id, const QList<qint64> &trackIds, std::optional<int> beforePosition = std::nullopt);
    /// 按曲目 id 移除（只能对 Manual）
    core::Result<void> removeTracks(qint64 id, const QList<qint64> &trackIds);
    /// 把这些曲目（保持其相对顺序）移动到 beforePosition 之前（beforePosition 以移动前的位置计；==
    /// count 表示移到末尾）
    core::Result<void> moveTracks(qint64 id, const QList<qint64> &trackIds, int beforePosition);

private:
    Database &m_db;
};

} // namespace linernotes::library
