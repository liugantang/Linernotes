// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <core/Result.h>
#include <library/CoverStore.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct CoverArtTarget {
    qint64 albumId = 0;
    QString releaseId { };
    QString releaseGroupId { }; // 可能为空
};

class CoverArtSource {
public:
    explicit CoverArtSource(library::Database &db);

    /// albums.cover_id IS NULL 且 mb_album_matches.status = 'matched' 且 release_id 非空 且
    /// cover_checked_at IS NULL，按 id 升序。
    [[nodiscard]] core::Result<QList<qint64>> pendingAlbums() const;

    /// 不再满足上述条件（已被处理、已有封面、匹配被删）→ std::nullopt。
    [[nodiscard]] core::Result<std::optional<CoverArtTarget>> load(qint64 albumId) const;

    /// 一个事务：INSERT covers(hash, mime, width, height, source='online', source_path=url,
    /// created_at) ON CONFLICT(hash) DO NOTHING； 按 hash 取 id；UPDATE albums SET cover_id = ?
    /// WHERE id = ? AND cover_id IS NULL；标记 cover_checked_at。
    core::Result<void> saveCover(
        qint64 albumId, const library::CoverStore::Info &info, const QUrl &url, qint64 now) const;

    /// 没找到封面：只标记 cover_checked_at。
    core::Result<void> markChecked(qint64 albumId, qint64 now) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::butler
