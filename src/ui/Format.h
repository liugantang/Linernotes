// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QString>

#include <library/LibraryEnums.h>

#include <algorithm>
#include <cstdint>

namespace linernotes::ui {

inline QString formatDuration(qint64 durationMs)
{
    const qint64 totalSeconds = std::max(0LL, durationMs) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

inline QString formatDateTime(qint64 epochMs)
{
    if (epochMs <= 0) {
        return { };
    }
    return QDateTime::fromMSecsSinceEpoch(epochMs).toLocalTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm"));
}

inline QString formatTagField(library::TagField field)
{
    switch (field) {
    case library::TagField::Title:
        //: Tag field name for track title
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Title");
    case library::TagField::Artist:
        //: Tag field name for track artist
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Artist");
    case library::TagField::Album:
        //: Tag field name for album name
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Album");
    case library::TagField::AlbumArtist:
        //: Tag field name for album artist
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Album Artist");
    case library::TagField::Genre:
        //: Tag field name for music genre
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Genre");
    case library::TagField::Composer:
        //: Tag field name for music composer
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Composer");
    case library::TagField::Year:
        //: Tag field name for release year
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Year");
    case library::TagField::TrackNumber:
        //: Tag field name for track number
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Track Number");
    case library::TagField::TrackTotal:
        //: Tag field name for total track count in album
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Track Total");
    case library::TagField::DiscNumber:
        //: Tag field name for disc number
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Disc Number");
    case library::TagField::DiscTotal:
        //: Tag field name for total disc count in album
        return QCoreApplication::translate("linernotes::ui::TagEditorModel", "Disc Total");
    }
    return { };
}

} // namespace linernotes::ui
