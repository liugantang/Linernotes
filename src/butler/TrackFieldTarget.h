// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <library/LibraryEnums.h>

#include <cstdint>

namespace linernotes::butler {

struct TrackFieldTarget {
    qint64 trackId = 0;
    library::TagField field = library::TagField::Artist;

    bool operator==(const TrackFieldTarget &) const = default;
};

} // namespace linernotes::butler
