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

inline constexpr QLatin1StringView kArtistCreditInvalidKey { "artist_credit.invalid_key" };
inline constexpr QLatin1StringView kArtistCreditInvalidResult { "artist_credit.invalid_result" };
inline constexpr QLatin1StringView kArtistCreditItemNotFound { "artist_credit.item_not_found" };
inline constexpr QLatin1StringView kArtistCreditSchemaNotFound { "artist_credit.schema_not_found" };
inline constexpr QLatin1StringView kArtistCreditPromptRenderFailed {
    "artist_credit.prompt_render_failed"
};

inline constexpr QLatin1StringView kArtistMergeInvalidKey { "artist_merge.invalid_key" };
inline constexpr QLatin1StringView kArtistMergeInvalidResult { "artist_merge.invalid_result" };
inline constexpr QLatin1StringView kArtistMergeItemNotFound { "artist_merge.item_not_found" };
inline constexpr QLatin1StringView kArtistMergeSchemaNotFound { "artist_merge.schema_not_found" };
inline constexpr QLatin1StringView kArtistMergePromptRenderFailed {
    "artist_merge.prompt_render_failed"
};

inline constexpr QLatin1StringView kMbInvalidResponse { "mb.invalid_response" };
inline constexpr QLatin1StringView kMbRateLimited { "mb.rate_limited" };
inline constexpr QLatin1StringView kMbNetwork { "mb.network" };
inline constexpr QLatin1StringView kFingerprintInvalidKey { "fingerprint.invalid_key" };

} // namespace linernotes::butler::errc
