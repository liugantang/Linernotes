// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QStringView>

#include <cstdint>

namespace linernotes::core {

Q_NAMESPACE

enum class PlaySource : std::uint8_t {
    Unknown,
    Library,
    Album,
    Artist,
    Playlist,
    Search,
    Queue,
    Nlq,
    Dj,
    External
};
Q_ENUM_NS(PlaySource)

[[nodiscard]] QString playSourceToString(PlaySource source);
[[nodiscard]] PlaySource playSourceFromString(QStringView str);

} // namespace linernotes::core
