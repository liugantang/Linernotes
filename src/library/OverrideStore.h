// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <core/Result.h>
#include <library/LibraryEnums.h>

#include <cstdint>
#include <optional>

namespace linernotes::library {

class Database;

struct TagEdit {
    TagField field = TagField::Title;
    std::optional<QString> value; // nullopt = 删除覆写（恢复为文件标签/修正层的值）；"" = 覆写为空

    bool operator==(const TagEdit &other) const = default;
};

class OverrideStore {
public:
    explicit OverrideStore(Database &db);

    /// 当前生效值（effective_metadata），数值字段转成字符串，NULL 为空串
    core::Result<QHash<TagField, QString>> effectiveValues(qint64 trackId) const;

    /// 该曲目哪些字段存在 user_overrides 行
    core::Result<QSet<TagField>> overriddenFields(qint64 trackId) const;

    /// 一个事务：对每首曲目按 edits 插入/更新/删除 user_overrides；随后对每首
    /// EntityLinker::linkTrack， 再 removeOrphans()，最后
    /// SearchIndex::flushDirty()。任一步失败整体回滚。
    core::Result<void> apply(const QList<qint64> &trackIds, const QList<TagEdit> &edits);

private:
    Database &m_db;
};

} // namespace linernotes::library
