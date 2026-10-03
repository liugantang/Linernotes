// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>

namespace linernotes::guide::errc {

inline constexpr QLatin1StringView kDbFailed { "guide.db_failed" };
inline constexpr QLatin1StringView kNetworkFailed { "guide.network_failed" };
inline constexpr QLatin1StringView kParseFailed { "guide.parse_failed" };

} // namespace linernotes::guide::errc
