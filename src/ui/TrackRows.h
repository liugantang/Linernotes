// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QVariantList>

#include <library/LibraryQuery.h>

namespace linernotes::ui {

[[nodiscard]] QVariantList trackRowsToVariant(const QList<library::TrackRow> &tracks);

} // namespace linernotes::ui
