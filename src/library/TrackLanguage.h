// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

#include <library/LibraryEnums.h>

namespace linernotes::library {

TrackLanguage inferTrackLanguage(const QString &title, const QString &album, const QString &artist);

} // namespace linernotes::library
