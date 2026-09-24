#pragma once

#include <QString>

#include <core/Settings.h>

namespace aimusic::core {

using namespace Qt::StringLiterals;

// NOLINTNEXTLINE(bugprone-throwing-static-initialization) - QString constructor is non-noexcept in
// Qt6
inline const SettingKey<QString> kLogLevel { u"log/level", u"info"_s };

} // namespace aimusic::core
