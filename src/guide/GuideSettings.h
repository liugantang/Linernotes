// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <core/Settings.h>

#include <cstdint>

namespace linernotes::guide {

using namespace Qt::StringLiterals;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization)
inline const core::SettingKey<qint64> kGuideLastPrewarmAt { u"guide/lastPrewarmAt", 0 };

} // namespace linernotes::guide
