// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <cstdint>

namespace linernotes::guide {

Q_NAMESPACE
Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")

enum class LyricsSource : std::uint8_t { Manual, Embedded, LrcFile, Lrclib };
Q_ENUM_NS(LyricsSource)

enum class LyricsStatus : std::uint8_t { Found, Instrumental, NotFound, Failed };
Q_ENUM_NS(LyricsStatus)

enum class KnowledgeKind : std::uint8_t { Artist, Song, Album };
Q_ENUM_NS(KnowledgeKind)

} // namespace linernotes::guide
