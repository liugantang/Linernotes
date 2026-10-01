// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct VersionLinkStats {
    int tracks = 0;
    int works = 0;
    int unresolved = 0;
    bool operator==(const VersionLinkStats &) const = default;
};

class VersionLinker {
public:
    VersionLinker(library::Database &db, const core::Clock &clock);

    /// 全量重算（可在工作线程调用，使用本线程的 connection()）。一个事务：
    /// 1. 读 VersionSuffixStore::loadAll(suffixPromptVersion) 与
    /// TitleMatchStore::loadAll(titleMatchPromptVersion)；
    ///    classify = classifySuffix(s)，若为 Unknown 再查缓存 verdict（按 suffixKey），仍没有则
    ///    Unknown。
    /// 2. 遍历所有曲目：effective_metadata.title（为空的跳过：work_id 置 NULL、删除其
    /// track_versions 行）；
    ///    主艺人 = track_artists 中 role = 'artist' 且 position = 0 的 artist_id。
    /// 3. resolveTitle → base/type/unresolved；写 track_versions（INSERT OR REPLACE）。
    /// 4. 作品键 grouping_key = exactKey(baseTitle) + "\x1f" + 主艺人 id（十进制）；没有主艺人时用
    /// "t" + track_id（单独成作品，
    ///    不把不同来源的同名无艺人曲目并在一起）。exactKey(baseTitle) 为空时同样按 "t" + track_id。
    /// 5. 同一主艺人下，若两首曲目的 exactKey(baseTitle) 构成 title_matches 中已确认（same &&
    /// confidence >= kTitleMatchMinConfidence）的对，
    ///    把它们的 groupingKey 用并查集连起来；每个连通集合统一改用其中字典序最小的 groupingKey。
    /// 6. works 按 grouping_key 复用已有行（保持 id 稳定）或新建；title = 组内 type 为 studio
    /// 的曲目中最小 track_id 的 baseTitle，
    ///    没有 studio 的取最小 track_id 的 baseTitle；已有行的 title 变了就更新。
    /// 7. UPDATE tracks SET work_id；最后删除没有任何曲目引用的 works。
    core::Result<VersionLinkStats> linkAll(
        int suffixPromptVersion, int titleMatchPromptVersion) const;

    /// 体检用：有标题但没有 track_versions 行、或 unresolved = 1 的曲目数。
    core::Result<int> countPending() const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
