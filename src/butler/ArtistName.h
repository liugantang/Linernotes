// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QStringView>

namespace linernotes::butler {

/// 精确键：NFKC → 大小写折叠 → 繁转简（ICU "Traditional-Simplified"）→ 去掉所有空白与标点/符号
/// （Unicode 类别 P*、S*、Z*；保留字母、数字、各种文字）。键相同即“写法差异”。
QString exactKey(QStringView name);

/// 罗马字键：用于把假名写法与罗马字写法连到一起。
/// - 含汉字（QChar::Script_Han）或谚文的名字 → 返回空（汉字读音不能机械确定）。
/// - 含平假名/片假名 → ICU 转写 "Hiragana-Latin; Katakana-Latin; NFD; [:Nonspacing Mark:] Remove;
/// Latin-ASCII; NFC"。
/// - 只含拉丁字母、数字、空白和标点符号 → 原文同样走 "NFD; [:Nonspacing Mark:] Remove; Latin-ASCII;
/// NFC"。
/// - 其他文字（西里尔、泰文等）→ 返回空。
/// 然后：小写，只保留 ASCII 字母与数字，再依次把 "ou"→"o"、"oo"→"o"、"uu"→"u"（长音写法差异）。
/// 结果为空或长度 < 4 时返回空（太短的键容易误连）。
QString romanKey(QStringView name);

} // namespace linernotes::butler
