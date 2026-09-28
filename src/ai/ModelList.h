// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QStringList>

#include <core/Result.h>

namespace linernotes::ai {

/// 解析 OpenAI 兼容 /models 响应：取 data[].id，去空、去重，按不区分大小写的字母序排序。
/// 顶层不是对象 / 没有 data 数组 → core::Error(errc::kBadResponse)。data 为空数组 →
/// 成功返回空列表。
core::Result<QStringList> parseModelList(const QByteArray &json);

} // namespace linernotes::ai
