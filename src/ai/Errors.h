// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>

namespace linernotes::ai::errc {

inline constexpr QLatin1StringView kNetwork { "ai.network" };
inline constexpr QLatin1StringView kTimeout { "ai.timeout" };
inline constexpr QLatin1StringView kAuth { "ai.auth" };
inline constexpr QLatin1StringView kRateLimited { "ai.rate_limited" };
inline constexpr QLatin1StringView kHttp { "ai.http" };
inline constexpr QLatin1StringView kBadResponse { "ai.bad_response" };
inline constexpr QLatin1StringView kAborted { "ai.aborted" };

} // namespace linernotes::ai::errc
