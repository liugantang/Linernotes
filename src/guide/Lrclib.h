// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <core/Result.h>
#include <library/LibraryQuery.h>

#include <cstdint>
#include <optional>

namespace linernotes::guide {

struct LrclibQuery {
    QString title { };
    QString artist { };
    QString album { };
    int durationSec = 0;
    bool operator==(const LrclibQuery &) const = default;
};

struct LrclibRecord {
    qint64 id = 0;
    QString trackName { };
    QString artistName { };
    QString albumName { };
    double durationSec = 0.0;
    bool instrumental = false;
    QString plainLyrics { };
    QString syncedLyrics { };
    bool operator==(const LrclibRecord &) const = default;
};

LrclibQuery lrclibQueryFor(const library::TrackRow &track);
QUrl lrclibGetUrl(const LrclibQuery &q);
QUrl lrclibSearchUrl(const LrclibQuery &q);
core::Result<LrclibRecord> parseLrclibRecord(const QByteArray &json);
core::Result<QList<LrclibRecord>> parseLrclibSearch(const QByteArray &json);
std::optional<LrclibRecord> pickSearchResult(
    const QList<LrclibRecord> &records, const LrclibQuery &q);

} // namespace linernotes::guide
