// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QtGlobal>

#include <core/Result.h>
#include <library/Database.h>
#include <library/PlayCountRule.h>
#include <nlq/NlqQuery.h>

namespace linernotes::nlq {

class QueryRunner {
public:
    QueryRunner(library::Database &db, library::PlayCountRule countRule);
    /// 返回按排序后的实体 id（曲目 id / 专辑 id / 艺人 id），最多 limit 个。
    [[nodiscard]] core::Result<QList<qint64>> run(const Query &query) const;

private:
    library::Database &m_db;
    library::PlayCountRule m_countRule;
};

} // namespace linernotes::nlq
