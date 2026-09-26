// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <cstdint>

namespace linernotes::player {

Q_NAMESPACE

enum class PlayMode : std::uint8_t { Sequential, RepeatAll, RepeatOne, Shuffle };
Q_ENUM_NS(PlayMode)

} // namespace linernotes::player
