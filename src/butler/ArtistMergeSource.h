// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <butler/ArtistGroup.h>
#include <core/Result.h>

#include <cstdint>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class ArtistMergeSource {
public:
    explicit ArtistMergeSource(library::Database &db);

    /// artists 表全部艺人，trackCount = track_artists 中的曲目数（role 不限，去重）。
    core::Result<QList<ArtistEntry>> loadArtists() const;

    /// 从 artist_credits 全部缓存结果构造：对每个 performer，
    /// altNames[exactKey(name)] 追加它的全部 aka（去重）。
    core::Result<QHash<QString, QStringList>> loadAltNames() const;

    /// 生成 item key（紧凑 JSON）：
    /// - {"type":"group","ids":[...]}：exactOnly 的组，每组一个 item；
    /// - {"type":"confirm","groups":[[...],[...]]}：非 exactOnly 的组，每 10 组一个 item；
    /// - {"type":"mb","id":N}：useMusicBrainz 时，名字含汉字/假名/韩文的艺人每人一个。
    /// 跳过：已有 pending 的 artist 别名修正涉及的艺人（作为 entity_id 或者名字等于某条 pending
    /// 修正的 new_value），以及艺人名等于某条 pending 的 artist_credit 修正的 old_value。
    core::Result<QStringList> findItems(bool useMusicBrainz) const;

    /// 为 LLM 准备上下文：该艺人曲目的不同专辑名，按曲目数降序。
    core::Result<QStringList> sampleAlbums(qint64 artistId, int limit = 3) const;

private:
    library::Database &m_db;
};

} // namespace linernotes::butler
