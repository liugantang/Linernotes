// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>

namespace linernotes::nlq::errc {

inline constexpr QLatin1StringView kQueryInvalid { "nlq.query_invalid" };
inline constexpr QLatin1StringView kInvalidResult { "nlq.invalid_result" };
inline constexpr QLatin1StringView kSchemaNotFound { "nlq.schema_not_found" };
inline constexpr QLatin1StringView kPromptRenderFailed { "nlq.prompt_render_failed" };

} // namespace linernotes::nlq::errc
