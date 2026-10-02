// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackRows.h"

#include <QString>
#include <QVariantMap>

#include <ui/Format.h>

namespace linernotes::ui {

QVariantList trackRowsToVariant(const QList<library::TrackRow> &tracks)
{
    QVariantList rows;
    rows.reserve(tracks.size());
    for (const auto &t : tracks) {
        QVariantMap map;
        map.insert(QStringLiteral("trackId"), t.trackId);
        map.insert(QStringLiteral("title"), t.title);
        map.insert(QStringLiteral("artist"), t.artist);
        map.insert(QStringLiteral("album"), t.album);
        map.insert(QStringLiteral("albumId"), t.albumId.value_or(0));
        map.insert(QStringLiteral("durationText"), formatDuration(t.durationMs));
        map.insert(QStringLiteral("coverHash"), t.coverHash);
        rows.append(map);
    }
    return rows;
}

} // namespace linernotes::ui
