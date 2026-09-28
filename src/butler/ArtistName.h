// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace linernotes::butler {

/// 是否包含汉字、假名或韩文（用于区分 CJK 名与罗马字名）。
bool containsCjk(QStringView text);

/// 精确键：NFKC → 大小写折叠 → 繁转简（ICU "Traditional-Simplified"）→ 去掉所有空白与标点/符号
/// （Unicode 类别 P*、S*、Z*；保留字母、数字、各种文字）。键相同即“写法差异”。
QString exactKey(QStringView name);

/// 罗马字词元：ICU "Any-Latin; Latin-ASCII" 转写 → 小写 → 按非字母数字切分 → 每个词做长音归一
/// （ou→o、oo→o、uu→u、ei 不动，词尾 h 去掉，如 "ohno"→"ono"）→
/// 排序。用于识别姓名顺序颠倒与罗马字变体。 注意 ICU
/// 把汉字转成汉语拼音；这对日文汉字名没有意义，所以只对原名不含汉字/假名/韩文的名字有效， 含 CJK
/// 的名字返回空列表。
QStringList romanTokens(QStringView name);

} // namespace linernotes::butler
