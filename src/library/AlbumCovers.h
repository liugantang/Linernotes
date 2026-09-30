// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QUrl>

#include <core/Result.h>
#include <library/CoverStore.h>

#include <cstdint>

namespace linernotes::library {

class Database;

/// 一个事务：INSERT covers(hash, mime, width, height, source='online', source_path=url, created_at)
/// ON CONFLICT(hash) DO NOTHING；按 hash 取 id；UPDATE albums SET cover_id = ? WHERE id =
/// ?（用户主动选择，允许覆盖）
core::Result<void> setOnlineAlbumCover(
    Database &db, qint64 albumId, const CoverStore::Info &info, const QUrl &url, qint64 now);

} // namespace linernotes::library
