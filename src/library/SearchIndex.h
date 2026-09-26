// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <core/Result.h>

#include <cstdint>

namespace linernotes::library {

struct SearchHit {
    qint64 trackId = 0;
    double rank = 0;

    bool operator==(const SearchHit &other) const = default;
};

/// FTS5 搜索索引管理器。
///
/// 约定与生命周期：
/// - 扫描器（Scanner）在每批事务提交前调用 flushDirty() 同步更新搜索索引；
/// - 阶段 6 UI 覆写 / 修正（user_overrides / corrections）修改数据后，同样调用 flushDirty()；
/// - flushDirty() 由调用方负责事务保护（调用方开启与提交事务）。
class SearchIndex {
public:
    explicit SearchIndex(const QSqlDatabase &db);

    /// 重建 search_dirty 中所有 track 的索引行，然后清空 search_dirty。
    /// 调用方负责事务。返回处理数。
    core::Result<int> flushDirty();

    /// 搜索：按 bm25 排序（列权重 title 10、artist 6、album_artist 5、album 4、composer 2、aliases
    /// 4、romanized 3）， 最多 limit 条。空查询返回空列表。
    core::Result<QList<SearchHit>> search(const QString &userInput, int limit = 200) const;

private:
    QSqlDatabase m_db;
};

} // namespace linernotes::library
