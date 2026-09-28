// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <cstdint>

namespace linernotes::library::artist_names {

Q_NAMESPACE

enum class Preference : std::uint8_t { Original, SimplifiedChinese, English };
Q_ENUM_NS(Preference)

} // namespace linernotes::library::artist_names

namespace linernotes::library {

using ArtistNamePreference = artist_names::Preference;

} // namespace linernotes::library
