// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QStringList>

#include <core/Result.h>
#include <library/Database.h>
#include <nlq/NlqQuery.h>

namespace linernotes::nlq {

struct PrunedVariants {
    int conditionIndex = 0;
    QStringList kept;
    QStringList dropped;
    bool operator==(const PrunedVariants &) const = default;
};

[[nodiscard]] core::Result<Query> pruneTextVariants(
    library::Database &db, const Query &query, QList<PrunedVariants> *report = nullptr);

} // namespace linernotes::nlq
