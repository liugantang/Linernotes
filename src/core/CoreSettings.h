// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <core/Settings.h>

namespace linernotes::core {

using namespace Qt::StringLiterals;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const SettingKey<QString> kLogLevel { u"log/level", u"info"_s };

} // namespace linernotes::core
