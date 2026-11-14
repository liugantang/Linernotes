// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Lrclib.h"

#include "Errors.h"
#include "GuideUtils.h"
#include "Knowledge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QUrlQuery>

#include <butler/TitleVersion.h>

#include <cmath>
#include <limits>

namespace linernotes::guide {

namespace {

int recordTier(const LrclibRecord &rec)
{
    if (!rec.syncedLyrics.trimmed().isEmpty()) {
        return 3;
    }
    if (!rec.plainLyrics.trimmed().isEmpty()) {
        return 2;
    }
    if (rec.instrumental) {
        return 1;
    }
    return 0;
}

} // namespace

LrclibQuery lrclibQueryFor(const library::TrackRow &track)
{
    LrclibQuery query;
    const auto resolved = butler::resolveTitle(track.title, butler::classifySuffix);
    query.title = resolved.baseTitle;
    query.artist = primaryArtist(track.artist);
    query.album = track.album;
    if (track.durationMs > 0) {
        query.durationSec
            = static_cast<int>(std::round(static_cast<double>(track.durationMs) / 1000.0));
    } else {
        query.durationSec = 0;
    }
    return query;
}

QUrl lrclibGetUrl(const LrclibQuery &q)
{
    QUrl url(QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), queryValue(q.title));
    query.addQueryItem(QStringLiteral("artist_name"), queryValue(q.artist));
    if (!q.album.isEmpty()) {
        query.addQueryItem(QStringLiteral("album_name"), queryValue(q.album));
    }
    if (q.durationSec > 0) {
        query.addQueryItem(QStringLiteral("duration"), QString::number(q.durationSec));
    }
    url.setQuery(query);
    return url;
}

QUrl lrclibSearchUrl(const LrclibQuery &q)
{
    QUrl url(QStringLiteral("https://lrclib.net/api/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("track_name"), queryValue(q.title));
    query.addQueryItem(QStringLiteral("artist_name"), queryValue(q.artist));
    url.setQuery(query);
    return url;
}

core::Result<LrclibRecord> parseLrclibRecord(const QByteArray &json)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kParseFailed),
            .message = QStringLiteral("Failed to parse LRCLIB record"),
            .detail = parseErr.error != QJsonParseError::NoError
                ? parseErr.errorString()
                : QStringLiteral("Invalid JSON object"),
        };
    }

    const QJsonObject obj = doc.object();
    LrclibRecord rec;
    rec.id = obj.value(QStringLiteral("id")).toInteger();
    rec.trackName = obj.value(QStringLiteral("trackName")).toString();
    rec.artistName = obj.value(QStringLiteral("artistName")).toString();
    rec.albumName = obj.value(QStringLiteral("albumName")).toString();
    rec.durationSec = obj.value(QStringLiteral("duration")).toDouble();
    rec.instrumental = obj.value(QStringLiteral("instrumental")).toBool();
    rec.plainLyrics = obj.value(QStringLiteral("plainLyrics")).toString();
    rec.syncedLyrics = obj.value(QStringLiteral("syncedLyrics")).toString();
    return rec;
}

core::Result<QList<LrclibRecord>> parseLrclibSearch(const QByteArray &json)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
        return core::Error {
            .code = QString(errc::kParseFailed),
            .message = QStringLiteral("Failed to parse LRCLIB search results"),
            .detail = parseErr.error != QJsonParseError::NoError
                ? parseErr.errorString()
                : QStringLiteral("Invalid JSON array"),
        };
    }

    const QJsonArray arr = doc.array();
    QList<LrclibRecord> list;
    list.reserve(arr.size());
    for (const auto &val : arr) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();
        LrclibRecord rec;
        rec.id = obj.value(QStringLiteral("id")).toInteger();
        rec.trackName = obj.value(QStringLiteral("trackName")).toString();
        rec.artistName = obj.value(QStringLiteral("artistName")).toString();
        rec.albumName = obj.value(QStringLiteral("albumName")).toString();
        rec.durationSec = obj.value(QStringLiteral("duration")).toDouble();
        rec.instrumental = obj.value(QStringLiteral("instrumental")).toBool();
        rec.plainLyrics = obj.value(QStringLiteral("plainLyrics")).toString();
        rec.syncedLyrics = obj.value(QStringLiteral("syncedLyrics")).toString();
        list.append(rec);
    }
    return list;
}

std::optional<LrclibRecord> pickSearchResult(
    const QList<LrclibRecord> &records, const LrclibQuery &q)
{
    const LrclibRecord *bestRecord = nullptr;
    int bestTier = 0;
    double bestDiff = std::numeric_limits<double>::max();

    for (const auto &rec : records) {
        const double diff = std::abs(rec.durationSec - static_cast<double>(q.durationSec));
        if (diff > 3.0) {
            continue;
        }
        const int tier = recordTier(rec);
        if (tier == 0) {
            continue;
        }
        if (tier > bestTier || (tier == bestTier && diff < bestDiff)) {
            bestTier = tier;
            bestDiff = diff;
            bestRecord = &rec;
        }
    }

    if (bestRecord != nullptr) {
        return *bestRecord;
    }
    return std::nullopt;
}

} // namespace linernotes::guide
