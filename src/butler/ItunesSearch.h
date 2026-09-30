// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUrl>

#include <core/Result.h>

#include <array>
#include <cstdint>
#include <optional>

namespace linernotes::butler {

struct ItunesAlbum {
    qint64 collectionId = 0;
    QString title { };
    QString artist { };
    std::optional<int> year = std::nullopt;
    int trackCount = 0;
    QString country { }; // 请求用的地区代码，如 "tw"
    QUrl artworkUrl100 { };
};

inline constexpr std::array kItunesCountries { "tw", "hk", "jp",
    "us" }; // 国区不可用（实测全部返回 0 条）

// 查询词：专辑艺人为空时只用专辑名；含汉字且 ICU "Hans-Hant"
// 转换结果不同时，额外返回繁体写法的查询词（去重）
QStringList itunesSearchTerms(const QString &albumTitle, const QString &albumArtist);
QUrl itunesSearchUrl(const QString &term, QStringView country);
core::Result<QList<ItunesAlbum>> parseItunesSearch(const QByteArray &json, const QString &country);
QUrl itunesArtworkUrl(const QUrl &artworkUrl100, int size); // 末尾不是 100x100bb.* 时原样返回
// 多个请求结果合并：按 collectionId 去重（保留先出现的），保持出现顺序
QList<ItunesAlbum> mergeItunesResults(const QList<QList<ItunesAlbum>> &perRequest);

} // namespace linernotes::butler
