// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QtGlobal>

#include <butler/DuplicateFinder.h>
#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class DuplicateSource {
public:
    DuplicateSource(library::Database &db, const core::Clock &clock);

    /// 读所有未缺失文件的曲目（cue 曲目 tracks.cue_index 非空的跳过：同一文件的分轨不是重复）：
    /// track_id、work_id、track_versions.version_type、files.duration_ms、content_hash、codec/sample_rate/bit_depth/bitrate、
    /// 标签完整度（effective_metadata 中
    /// title、artist、album、album_artist、year、track_number、genre 非空的个数）、
    /// 是否有封面（files.cover_id 或所属 album 的 cover_id 非空），
    /// withFingerprints 为 true 时附上 FingerprintStore::loadAll() 的指纹，keepScore 算好。
    core::Result<QList<DupTrack>> loadTracks(bool withFingerprints = true) const;

    /// 读出所有 duplicate_dismissals 中的 (track_a, track_b) 对，track_a < track_b
    core::Result<QSet<QPair<qint64, qint64>>> loadDismissals() const;

    /// 需要计算声学指纹的文件：属于某个候选簇（candidateClusters）的曲目所在文件中，FingerprintStore::pendingFileIds()
    /// 里有的。升序。
    core::Result<QList<qint64>> fingerprintCandidates() const;

    /// 全量替换：一个事务里 DELETE 全部旧组，再写入 groups（成员的 recommended、keep_score
    /// 一并写）。
    core::Result<void> saveGroups(
        const QList<DupGroup> &groups, const QList<DupTrack> &tracks) const;

    /// 体检/界面用：各 kind 的组数。
    core::Result<QHash<DuplicateKind, int>> countGroups() const;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
