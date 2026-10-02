// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QtGlobal>

#include <core/Result.h>
#include <rec/Recommender.h>

#include <optional>

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {

inline constexpr qint64 kSessionLookbackMs = 4LL * 60 * 60 * 1000;
inline constexpr int kSessionMaxEvents = 10;
inline constexpr double kSessionNegativeWeight = -0.7;
inline constexpr double kSessionRecencyDecay = 0.8;
inline constexpr double kSessionCurrentTrackWeight = 1.0;

[[nodiscard]] core::Result<QList<Seed>> sessionSeeds(
    library::Database &db, qint64 nowMs, std::optional<qint64> currentTrackId = std::nullopt);

} // namespace linernotes::rec
