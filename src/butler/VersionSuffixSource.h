// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <core/Result.h>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

struct SuffixSample {
    QString key { };
    QString suffix { };
    QString title { };
    bool operator==(const SuffixSample &) const = default;
};

/// 解析紧凑 JSON itemKey: {"items":[{"key":..,"suffix":..,"title":..}, ...]}
core::Result<QList<SuffixSample>> parseSuffixItemKey(const QString &itemKey);

class VersionSuffixSource {
public:
    static constexpr qsizetype kBatchSize = 80;

    explicit VersionSuffixSource(library::Database &db);

    /// 遍历 effective_metadata.title（非空），splitSuffixes，对每个后缀 classifySuffix；
    /// Unknown 的按 suffixKey 去重，排除 version_suffixes 中已有（prompt_version 相同）的键；
    /// 每个键保留一个示例：后缀原文 + 所在的完整标题（第一次遇到的）。
    /// 按键排序后每 kBatchSize 个组成一个 itemKey：紧凑 JSON
    /// {"items":[{"key":..,"suffix":..,"title":..}, ...]}
    core::Result<QStringList> findItems(int promptVersion) const;

    /// 同上但只返回去重后未知后缀的数量（体检用）
    core::Result<int> countPending(int promptVersion) const;

private:
    core::Result<QList<SuffixSample>> collectPendingSamples(int promptVersion) const;

    library::Database &m_db;
};

} // namespace linernotes::butler
