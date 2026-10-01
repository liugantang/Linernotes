// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <core/Result.h>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct TitleMatchTrack {
    qint64 trackId = 0;
    qint64 workId = 0;
    qint64 mainArtistId = 0;
    QString artistName { };
    QString baseTitle { }; // works.title
    QString versionType { }; // track_versions.version_type 原样
    qint64 durationMs = 0;
    bool operator==(const TitleMatchTrack &) const = default;
};

struct TitlePair {
    QString titleA { }; // 原文；按 key 排序后 keyA < keyB
    QString titleB { };
    QString keyA { }; // exactKey(titleA)
    QString keyB { };
    QString artist { }; // 主艺人名，给模型作上下文
    bool operator==(const TitlePair &) const = default;
};

/// 标题是否“只用拉丁字母书写”：不含汉字、平假名、片假名、谚文（QChar::script
/// 判断；数字、标点、空白不影响）。
bool isLatinOnlyTitle(const QString &title);

/// 候选对：按 mainArtistId 分组（0 跳过），组内按 durationMs 排序，两首时长差 ≤
/// kDurationToleranceMs（DuplicateFinder.h）、 workId 不同、versionType 相同、且恰好一首
/// isLatinOnlyTitle 的，生成一对。 按 (keyA, keyB) 去重（keyA < keyB；任一 key 为空或两 key
/// 相等的跳过），结果按 (keyA, keyB) 排序。
QList<TitlePair> findTitlePairs(const QList<TitleMatchTrack> &tracks);

class TitleMatchSource {
public:
    static constexpr qsizetype kBatchSize = 50;
    explicit TitleMatchSource(library::Database &db);

    /// 读库构造 TitleMatchTrack（跳过 cue 分轨与文件缺失的曲目，口径与 DuplicateSource 一致；主艺人
    /// = track_artists role='artist' position=0）， findTitlePairs 后排除 title_matches
    /// 中已有（prompt_version 相同）的对，每 kBatchSize 对组成一个 itemKey： 紧凑 JSON
    /// {"items":[{"a":..,"b":..,"artist":..}, ...]}（a/b 为原文，顺序同 TitlePair）。
    core::Result<QStringList> findItems(int promptVersion) const;

    /// 待判断的对数（体检用）
    core::Result<int> countPending(int promptVersion) const;

private:
    core::Result<QList<TitlePair>> collectPendingPairs(int promptVersion) const;

    library::Database &m_db;
};

/// 解析 itemKey，key 用 exactKey 重新计算
core::Result<QList<TitlePair>> parseTitlePairItemKey(const QString &itemKey);

} // namespace linernotes::butler
