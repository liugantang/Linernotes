// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>
#include <QString>
#include <QStringList>

#include <core/Result.h>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

inline constexpr QLatin1StringView kTargetLangZhHans { "zh-Hans" };

/// 纯函数：判定是否需要翻译（含假名、谚文、或至少 2
/// 个拉丁字母；只由汉字、数字、标点、空白组成的不翻译）
bool needsTranslation(const QString &text);

/// 解析紧凑 JSON itemKey: {"items":["text1", "text2", ...]}
core::Result<QStringList> parseTranslationItemKey(const QString &itemKey);

class TranslationSource {
public:
    static constexpr qsizetype kBatchSize = 80;

    explicit TranslationSource(library::Database &db);

    /// 从 effective_metadata 中取 title 与 album 的去重非空值（trim 后），
    /// 过滤 needsTranslation，排除 text_translations 中已有（同 target_lang、同
    /// prompt_version）的文本， 排序后每 kBatchSize 个组成一个 itemKey：紧凑 JSON {"items":[...]}
    core::Result<QStringList> findItems(int promptVersion) const;

    /// 同上但只返回去重后待翻译文本的总数量（体检用）
    core::Result<int> countPending(int promptVersion) const;

private:
    core::Result<QStringList> collectPendingTexts(int promptVersion) const;

    library::Database &m_db;
};

} // namespace linernotes::butler
