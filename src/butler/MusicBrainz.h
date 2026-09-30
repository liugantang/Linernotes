// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <core/Result.h>

#include <cstdint>
#include <optional>

namespace linernotes::butler {

constexpr qint64 kMaxCacheAgeMs = 30LL * 24LL * 3600LL * 1000LL; // 30 days

struct MbTrack {
    int disc = 0; // media.position（从 1 开始）
    int position = 0; // track.position
    QString number { }; // track.number（原样字符串，如 "A1"）
    QString title { };
    QString artist { }; // 曲目的 artist-credit 拼接（name + joinphrase）；没有时取 release 的
    qint64 lengthMs = 0; // 缺失为 0
    QString recordingId { };
    bool operator==(const MbTrack &) const = default;
};

struct MbReleaseSummary { // 来自 release 搜索结果
    QString id { };
    int score = 0;
    QString title { };
    QString artist { }; // artist-credit 拼接
    QString date { }; // 原样，如 "2016-09-07" / "2016" / 空
    QString country { };
    int trackCount = 0; // 搜索结果的 track-count（视频介质不计入）
    int discCount = 0; // media 数（视频介质不计入）
    QStringList formats; // 各 media 的 format，缺失的跳过
    QString releaseGroupId { };
    QString primaryType { }; // release-group.primary-type，如 "Album" / "Single"
    QString status { }; // JSON 顶层 status，如 "Official" / "Pseudo-Release"，缺失为空
    bool operator==(const MbReleaseSummary &) const = default;
};

struct MbRelease { // 来自 release 详情
    QString id { };
    QString title { };
    QString artist { };
    QString date { };
    QString country { };
    QString originalDate { }; // release-group.first-release-date
    QString releaseGroupId { };
    QString primaryType { };
    QString status { }; // JSON 顶层 status，如 "Official" / "Pseudo-Release"，缺失为空
    QStringList labels; // label-info 中非空的 label.name，去重保序
    int discCount = 0; // 非视频介质数
    QList<MbTrack> tracks; // 按 (disc, position) 顺序（视频介质不计入）
    bool operator==(const MbRelease &) const = default;
};

struct MbReleaseRef {
    QString id { };
    QString title { };
    QString date { };
    bool operator==(const MbReleaseRef &) const = default;
};

struct MbRecordingHit { // 来自 recording 搜索
    QString id { };
    int score = 0;
    QString title { };
    QString artist { };
    qint64 lengthMs = 0;
    QList<MbReleaseRef> releases;
    bool operator==(const MbRecordingHit &) const = default;
};

core::Result<QList<MbReleaseSummary>> parseReleaseSearch(const QByteArray &json);
core::Result<MbRelease> parseRelease(const QByteArray &json);
core::Result<QList<MbRecordingHit>> parseRecordingSearch(const QByteArray &json);

/// 判断介质格式是否为视频格式（DVD、DVD-Video、Blu-ray 等）
[[nodiscard]] bool isVideoFormat(const QString &format);

/// 从日期字符串取年份（"2016-09-07" → 2016，"2016" → 2016，空或非法 → nullopt）
std::optional<int> yearFromDate(const QString &date);

/// Lucene 查询短语转义：对 + - && || ! ( ) { } [ ] ^ " ~ * ? : \ / 加反斜杠。
QString luceneEscape(const QString &text);

/// 构造请求 URL（fmt=json；base 为 https://musicbrainz.org/ws/2/）
/// release 搜索：query = release:(<title>)，artist 非空时追加 AND artist:(<artist>)；limit 参数。
QUrl releaseSearchUrl(const QString &title, const QString &artist, int limit = 10);
/// release 详情：inc=recordings+artist-credits+labels+release-groups
QUrl releaseUrl(const QString &releaseId);
/// recording 搜索：query = recording:(<title>)，artist 非空时追加 AND artist:(<artist>)。
QUrl recordingSearchUrl(const QString &title, const QString &artist, int limit = 10);

/// 共用的 MusicBrainz / Cover Art Archive User-Agent
QString userAgent();

} // namespace linernotes::butler
