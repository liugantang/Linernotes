// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <core/Result.h>
#include <library/LibraryQuery.h>

namespace linernotes::library {

/// 分组搜索结果。
struct SearchResults {
    QList<TrackRow> tracks; ///< 按相关度，最多 trackLimit 条
    QList<AlbumRow> albums; ///< 最多 groupLimit 个
    QList<ArtistRow> artists; ///< 最多 groupLimit 个

    bool operator==(const SearchResults &) const = default;
};

/// 负责曲库的分组搜索：结合 SearchIndex（FTS5 / 拼音 / 分词）与 LibraryQuery（按 ID 批量取行）。
class LibrarySearch {
public:
    explicit LibrarySearch(const QSqlDatabase &db);

    /// 按相关度分组搜索曲目、专辑与艺人。
    core::Result<SearchResults> search(
        const QString &userInput, int trackLimit = 200, int groupLimit = 8) const;

private:
    QSqlDatabase m_db;
};

} // namespace linernotes::library
