// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

namespace linernotes::library::search {

/// 规范化：NFKC（全角 → 半角等）+ 小写（Unicode case folding）。
QString normalize(const QString &text);

/// 用于文本列：normalize 后，CJK 字符（汉字、假名、谚文）之间插入空格，使每个字成为一个 token；
/// 非 CJK 部分保持原样。并追加简繁变体：若 ICU "Hans-Hant" / "Hant-Hans" 转换结果与原文不同，
/// 把转换后的（同样按字切分的）文本用空格追加在后面。
QString indexText(const QString &text);

/// 用于 romanized 列：
///  - 含汉字：ICU "Han-Latin; Latin-ASCII" 得到逐字拼音（如 "林晓风" → "lin xiao feng"），
///    输出 分音节形式 + 连写形式（"linxiaofeng"）+ 首字母（"lxf"）；
///  - 含假名/谚文：ICU "Any-Latin; Latin-ASCII" 的罗马字（分词形式 + 连写形式）；
///  - 纯拉丁文：返回空（文本列已覆盖）。
/// ICU Transliterator 创建开销大且非线程安全：每线程缓存一个实例（thread_local）。
/// Known limitation: 多音字取 ICU 默认读音，日文汉字按中文读音转拼音。
QString romanized(const QString &text);

/// 把用户输入编译为 FTS5 MATCH 表达式；输入无有效内容时返回空串。
QString buildMatchQuery(const QString &userInput);

} // namespace linernotes::library::search
