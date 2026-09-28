// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>
#include <QStringView>

namespace linernotes::butler {

/// 是否包含汉字、假名或韩文（用于区分 CJK 名与罗马字名）。
bool containsCjk(QStringView text);

/// 精确键：NFKC → 大小写折叠 → 繁转简（ICU "Traditional-Simplified"）→ 去掉所有空白与标点/符号
/// （Unicode 类别 P*、S*、Z*；保留字母、数字、各种文字）。键相同即“写法差异”。
QString exactKey(QStringView name);

} // namespace linernotes::butler
