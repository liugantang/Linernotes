// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <core/Result.h>

namespace linernotes::butler {

struct MbAlias {
    QString name;
    QString sortName;
    QString locale; // 可能为空；如 "en"、"ja"、"zh_Hans"
    bool primary = false;

    bool operator==(const MbAlias &) const = default;
};

struct MbArtist {
    QString mbid;
    QString name;
    QString sortName;
    QString type; // "Person" / "Group" / 空
    QString country;
    QString disambiguation;
    int score = 0; // 0–100
    QList<MbAlias> aliases;

    bool operator==(const MbArtist &) const = default;
};

/// 解析 /ws/2/artist/?query=...&fmt=json 的响应体。字段缺失按空值处理；顶层不是对象或没有
/// "artists" 数组 → errc::kMbInvalidResponse。
core::Result<QList<MbArtist>> parseArtistSearch(const QByteArray &body);

/// https://musicbrainz.org/ws/2/artist/?query=artist:"<name>"&fmt=json&limit=<limit>
/// name 中的 Lucene 特殊字符（+ - && || ! ( ) { } [ ] ^ " ~ * ? : \ /）用反斜杠转义，
/// 再整体放进双引号做短语查询；用 QUrlQuery 编码。
QUrl artistSearchUrl(const QString &name, int limit = 5);

} // namespace linernotes::butler
