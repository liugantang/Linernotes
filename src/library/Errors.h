// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QLatin1StringView>

namespace linernotes::library::errc {

inline constexpr QLatin1StringView kDbOpen { "db.open" };
inline constexpr QLatin1StringView kDbFts5 { "db.fts5" };
inline constexpr QLatin1StringView kDbMigration { "db.migration" };
inline constexpr QLatin1StringView kDbMigrationInvalid { "db.migration.invalid" };
inline constexpr QLatin1StringView kDbTooNew { "db.too_new" };
inline constexpr QLatin1StringView kDbFk { "db.fk" };
inline constexpr QLatin1StringView kDbBackup { "db.backup" };
inline constexpr QLatin1StringView kDbTransaction { "db.transaction" };
inline constexpr QLatin1StringView kDbDriver { "db.driver" };
inline constexpr QLatin1StringView kDbHandle { "db.handle" };
inline constexpr QLatin1StringView kDbQuery { "db.query" };
inline constexpr QLatin1StringView kFileRead { "file.read" };
inline constexpr QLatin1StringView kRootInvalid { "root.invalid" };
inline constexpr QLatin1StringView kRootOverlap { "root.overlap" };
inline constexpr QLatin1StringView kCoverDecode { "cover.decode" };
inline constexpr QLatin1StringView kTagRead { "tag.read" };
inline constexpr QLatin1StringView kTagUnsupported { "tag.unsupported" };
inline constexpr QLatin1StringView kPlaylistInvalid { "playlist.invalid" };
inline constexpr QLatin1StringView kPlaylistRuleInvalid { "playlist.rule_invalid" };
inline constexpr QLatin1StringView kRatingInvalid { "rating.invalid" };
inline constexpr QLatin1StringView kTagOverrideInvalid { "tag.override_invalid" };
inline constexpr QLatin1StringView kCorrectionInvalid { "correction.invalid" };
inline constexpr QLatin1StringView kCorrectionNotFound { "correction.not_found" };
inline constexpr QLatin1StringView kTrackNotFound { "track.not_found" };
inline constexpr QLatin1StringView kTrackMergeInvalid { "track.merge_invalid" };

} // namespace linernotes::library::errc
