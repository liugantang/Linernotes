// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include "Mojibake.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace linernotes::butler::internal {

/// 获取 ICU 转换器名称
const char *icuConverterName(SourceEncoding encoding);

/// 使用指定编码严格解码字节序列，遇非法序列返回 nullopt
std::optional<QString> strictDecode(const QByteArray &bytes, SourceEncoding encoding);

/// 计算解码后文本在指定编码下的得分，[0.0, 1.0]
double calculateEncodingScore(
    const QString &text, const QByteArray &rawBytes, SourceEncoding encoding);

/// 检查 uchardet 结果是否与指定候选编码同族
bool matchesUchardetFamily(const QByteArray &bytes, SourceEncoding encoding);

} // namespace linernotes::butler::internal
