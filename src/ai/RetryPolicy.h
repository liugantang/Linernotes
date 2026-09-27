// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QtGlobal>

#include <core/Result.h>

#include <optional>

namespace linernotes::ai {

struct RetryPolicy {
    int maxRetries = 3; // 不含首次请求
    qint64 baseDelayMs = 1000; // 第 n 次重试（从 1 起）等待 base * 2^(n-1)
    qint64 maxDelayMs = 60000;
};

/// 返回下一次重试前的等待毫秒数；不应重试时返回 nullopt。
/// 可重试：errc::kNetwork、kTimeout、kRateLimited、kHttp 且 httpStatus >= 500。
/// 不可重试：kAuth、kBadResponse、kAborted、其他 4xx、结构化解析失败等。
/// 有 retryAfterMs 时用 max(retryAfterMs, 退避值)，再截断到 maxDelayMs。
/// retriesDone >= maxRetries 时返回 nullopt。不加随机抖动（保证确定性）。
std::optional<qint64> retryDelayMs(const RetryPolicy &policy, const core::Error &error,
    int httpStatus, std::optional<qint64> retryAfterMs, int retriesDone);

} // namespace linernotes::ai
