// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <core/Result.h>

#include <cstdint>

namespace linernotes::library {

class Database;

class PlaylistStore {
public:
    explicit PlaylistStore(Database &db);

    /// 新建手动歌单并按顺序写入曲目（同一事务；position 从 0 递增；created_at/updated_at 为当前
    /// Unix 毫秒）。 name 去掉首尾空白后为空 → 错误。 playlists.position 取现有最大值 +
    /// 1。返回新歌单 id。
    core::Result<qint64> createManual(const QString &name, const QList<qint64> &trackIds);

private:
    Database &m_db;
};

} // namespace linernotes::library
