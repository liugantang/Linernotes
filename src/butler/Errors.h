// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>

namespace linernotes::butler::errc {

inline constexpr QLatin1StringView kMojibakeGroupNotFound { "mojibake.group_not_found" };
inline constexpr QLatin1StringView kMojibakeInvalidKey { "mojibake.invalid_key" };
inline constexpr QLatin1StringView kMojibakeInvalidResult { "mojibake.invalid_result" };
inline constexpr QLatin1StringView kMojibakeSchemaNotFound { "mojibake.schema_not_found" };
inline constexpr QLatin1StringView kMojibakePromptRenderFailed { "mojibake.prompt_render_failed" };

} // namespace linernotes::butler::errc
