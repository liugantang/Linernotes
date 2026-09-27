// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ai/Errors.h>
#include <ai/RetryPolicy.h>

#include <algorithm>

namespace linernotes::ai {

namespace {

bool isRetryable(const core::Error &error, int httpStatus)
{
    const auto &code = error.code;
    return code == errc::kNetwork || code == errc::kTimeout || code == errc::kRateLimited
        || (code == errc::kHttp && httpStatus >= 500);
}

} // namespace

std::optional<qint64> retryDelayMs(const RetryPolicy &policy, const core::Error &error,
    int httpStatus, std::optional<qint64> retryAfterMs, int retriesDone)
{
    if (retriesDone < 0 || retriesDone >= policy.maxRetries) {
        return std::nullopt;
    }

    if (!isRetryable(error, httpStatus)) {
        return std::nullopt;
    }

    qint64 backoff = std::max(static_cast<qint64>(0), policy.baseDelayMs);
    for (int i = 0; i < retriesDone; ++i) {
        backoff = std::min(backoff * 2, policy.maxDelayMs);
    }

    qint64 delay = backoff;
    if (retryAfterMs.has_value()) {
        delay = std::max(delay, *retryAfterMs);
    }

    delay = std::min(delay, policy.maxDelayMs);
    return std::max(static_cast<qint64>(0), delay);
}

} // namespace linernotes::ai
