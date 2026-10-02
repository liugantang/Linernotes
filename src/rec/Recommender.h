// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSet>
#include <QtGlobal>

#include <audio/EmbeddingIndex.h>
#include <core/Result.h>
#include <rec/CandidatePool.h>

namespace linernotes::core {
class Clock;
} // namespace linernotes::core

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {

class SoundIndex;

struct Seed {
    qint64 trackId = 0;
    double weight = 1.0; // 正：喜欢的方向；负：负种子（如被跳过的歌），使结果避开相似声音
    bool operator==(const Seed &) const = default;
};

struct RecRequest {
    QList<Seed> seeds; // 可为空（新用户无记录）：此时只按收藏/久未播放/随机打分
    int count = 30;
    QSet<qint64> exclude; // 额外排除（如当前队列已有的曲目）
    qint64 recentExcludeMs = 3LL * 24 * 3600 * 1000; // 此时间内播放过的不推荐；0 表示不限制
    double freshnessWeight = 0.5; // 久未播放/从未播放加分的权重；10.4 会调大
    double randomness = 0.0; // 随机扰动幅度；0 表示完全确定
    quint64 randomSeed = 0; // 扰动的随机种子（每日推荐用日期）
    int maxPerArtist = 2;
    int maxPerAlbum = 1;
};

// 纯函数，便于测试：不访问数据库
[[nodiscard]] QList<qint64> rank(const QList<Candidate> &candidates,
    const audio::EmbeddingIndex *index /* 可为 nullptr */, const RecRequest &request, qint64 nowMs);

class Recommender {
public:
    Recommender(library::Database &db, SoundIndex &soundIndex, const core::Clock &clock);

    // loadCandidates + soundIndex.index() + rank
    [[nodiscard]] core::Result<QList<qint64>> recommend(const RecRequest &request);

private:
    library::Database &m_db;
    SoundIndex &m_soundIndex;
    const core::Clock &m_clock;
};

} // namespace linernotes::rec
