// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MusicBrainz.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrlQuery>

#include <butler/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

QString joinArtistCredit(const QJsonValue &creditVal)
{
    if (!creditVal.isArray()) {
        return { };
    }
    const QJsonArray arr = creditVal.toArray();
    QString joined;
    for (const auto &itemVal : arr) {
        if (!itemVal.isObject()) {
            continue;
        }
        const QJsonObject itemObj = itemVal.toObject();
        QString name = itemObj.value(QStringLiteral("name")).toString();
        if (name.isEmpty() && itemObj.contains(QStringLiteral("artist"))
            && itemObj.value(QStringLiteral("artist")).isObject()) {
            name = itemObj.value(QStringLiteral("artist"))
                       .toObject()
                       .value(QStringLiteral("name"))
                       .toString();
        }
        joined.append(name);
        joined.append(itemObj.value(QStringLiteral("joinphrase")).toString());
    }
    return joined;
}

MbReleaseSummary parseSummary(const QJsonObject &relObj)
{
    MbReleaseSummary summary;
    summary.id = relObj.value(QStringLiteral("id")).toString();
    summary.score = relObj.value(QStringLiteral("score")).toInt(0);
    summary.title = relObj.value(QStringLiteral("title")).toString();
    summary.artist = joinArtistCredit(relObj.value(QStringLiteral("artist-credit")));
    summary.date = relObj.value(QStringLiteral("date")).toString();
    summary.country = relObj.value(QStringLiteral("country")).toString();
    summary.trackCount = relObj.value(QStringLiteral("track-count")).toInt(0);

    if (relObj.contains(QStringLiteral("media"))
        && relObj.value(QStringLiteral("media")).isArray()) {
        const QJsonArray mediaArr = relObj.value(QStringLiteral("media")).toArray();
        summary.discCount = static_cast<int>(mediaArr.size());
        for (const auto &mediaVal : mediaArr) {
            if (mediaVal.isObject()) {
                const QString fmt = mediaVal.toObject().value(QStringLiteral("format")).toString();
                if (!fmt.isEmpty()) {
                    summary.formats.append(fmt);
                }
            }
        }
    }

    if (relObj.contains(QStringLiteral("release-group"))
        && relObj.value(QStringLiteral("release-group")).isObject()) {
        const QJsonObject rgObj = relObj.value(QStringLiteral("release-group")).toObject();
        summary.releaseGroupId = rgObj.value(QStringLiteral("id")).toString();
        summary.primaryType = rgObj.value(QStringLiteral("primary-type")).toString();
    }

    return summary;
}

void parseReleaseGroup(const QJsonObject &rootObj, MbRelease &release)
{
    if (rootObj.contains(QStringLiteral("release-group"))
        && rootObj.value(QStringLiteral("release-group")).isObject()) {
        const QJsonObject rgObj = rootObj.value(QStringLiteral("release-group")).toObject();
        release.originalDate = rgObj.value(QStringLiteral("first-release-date")).toString();
        release.releaseGroupId = rgObj.value(QStringLiteral("id")).toString();
        release.primaryType = rgObj.value(QStringLiteral("primary-type")).toString();
    }
}

QStringList parseLabels(const QJsonArray &labelInfoArr)
{
    QStringList labels;
    for (const auto &infoVal : labelInfoArr) {
        if (!infoVal.isObject()) {
            continue;
        }
        const QJsonObject infoObj = infoVal.toObject();
        const QJsonValue labelVal = infoObj.value(QStringLiteral("label"));
        if (labelVal.isObject()) {
            const QString labelName = labelVal.toObject().value(QStringLiteral("name")).toString();
            if (!labelName.isEmpty() && !labels.contains(labelName)) {
                labels.append(labelName);
            }
        }
    }
    return labels;
}

MbTrack parseTrack(
    const QJsonObject &trackObj, int disc, qsizetype trackIdx, const QString &fallbackArtist)
{
    MbTrack track;
    track.disc = disc;
    track.position
        = trackObj.value(QStringLiteral("position")).toInt(static_cast<int>(trackIdx + 1));
    track.number = trackObj.value(QStringLiteral("number")).toString();
    track.title = trackObj.value(QStringLiteral("title")).toString();

    const QString trackArtist = joinArtistCredit(trackObj.value(QStringLiteral("artist-credit")));
    if (!trackArtist.isEmpty()) {
        track.artist = trackArtist;
    } else {
        track.artist = fallbackArtist;
    }

    track.lengthMs = trackObj.value(QStringLiteral("length")).toInteger(0);

    if (trackObj.contains(QStringLiteral("recording"))
        && trackObj.value(QStringLiteral("recording")).isObject()) {
        const QJsonObject recObj = trackObj.value(QStringLiteral("recording")).toObject();
        track.recordingId = recObj.value(QStringLiteral("id")).toString();
        if (track.title.isEmpty()) {
            track.title = recObj.value(QStringLiteral("title")).toString();
        }
        if (track.lengthMs == 0) {
            track.lengthMs = recObj.value(QStringLiteral("length")).toInteger(0);
        }
    }

    return track;
}

QList<MbTrack> parseMediaTracks(const QJsonArray &mediaArr, const QString &fallbackArtist)
{
    QList<MbTrack> tracks;
    for (qsizetype mediaIdx = 0; mediaIdx < mediaArr.size(); ++mediaIdx) {
        const auto &mediaVal = mediaArr.at(mediaIdx);
        if (!mediaVal.isObject()) {
            continue;
        }
        const QJsonObject mediaObj = mediaVal.toObject();
        int disc = mediaObj.value(QStringLiteral("position")).toInt(0);
        if (disc <= 0) {
            disc = static_cast<int>(mediaIdx + 1);
        }

        if (mediaObj.contains(QStringLiteral("tracks"))
            && mediaObj.value(QStringLiteral("tracks")).isArray()) {
            const QJsonArray tracksArr = mediaObj.value(QStringLiteral("tracks")).toArray();
            for (qsizetype trackIdx = 0; trackIdx < tracksArr.size(); ++trackIdx) {
                const auto &trackVal = tracksArr.at(trackIdx);
                if (!trackVal.isObject()) {
                    continue;
                }
                tracks.append(parseTrack(trackVal.toObject(), disc, trackIdx, fallbackArtist));
            }
        }
    }
    return tracks;
}

} // namespace

core::Result<QList<MbReleaseSummary>> parseReleaseSearch(const QByteArray &json)
{
    if (json.isEmpty()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Empty response body"),
            .detail = { },
        };
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Invalid JSON object in release search response"),
            .detail = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Top-level is not an object"),
        };
    }

    const QJsonObject rootObj = doc.object();
    if (!rootObj.contains(QStringLiteral("releases"))
        || !rootObj.value(QStringLiteral("releases")).isArray()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Missing 'releases' array in release search response"),
            .detail = { },
        };
    }

    const QJsonArray releasesArr = rootObj.value(QStringLiteral("releases")).toArray();
    QList<MbReleaseSummary> summaries;
    summaries.reserve(releasesArr.size());

    for (const auto &relVal : releasesArr) {
        if (!relVal.isObject()) {
            continue;
        }
        summaries.append(parseSummary(relVal.toObject()));
    }

    return summaries;
}

core::Result<MbRelease> parseRelease(const QByteArray &json)
{
    if (json.isEmpty()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Empty response body"),
            .detail = { },
        };
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Invalid JSON object in release response"),
            .detail = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Top-level is not an object"),
        };
    }

    const QJsonObject rootObj = doc.object();
    const QString id = rootObj.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Missing or empty 'id' in release response"),
            .detail = { },
        };
    }

    MbRelease release;
    release.id = id;
    release.title = rootObj.value(QStringLiteral("title")).toString();
    release.artist = joinArtistCredit(rootObj.value(QStringLiteral("artist-credit")));
    release.date = rootObj.value(QStringLiteral("date")).toString();
    release.country = rootObj.value(QStringLiteral("country")).toString();

    parseReleaseGroup(rootObj, release);

    if (rootObj.contains(QStringLiteral("label-info"))
        && rootObj.value(QStringLiteral("label-info")).isArray()) {
        release.labels = parseLabels(rootObj.value(QStringLiteral("label-info")).toArray());
    }

    if (rootObj.contains(QStringLiteral("media"))
        && rootObj.value(QStringLiteral("media")).isArray()) {
        const QJsonArray mediaArr = rootObj.value(QStringLiteral("media")).toArray();
        release.discCount = static_cast<int>(mediaArr.size());
        release.tracks = parseMediaTracks(mediaArr, release.artist);
    }

    std::ranges::stable_sort(release.tracks, [](const MbTrack &a, const MbTrack &b) {
        if (a.disc != b.disc) {
            return a.disc < b.disc;
        }
        return a.position < b.position;
    });

    return release;
}

core::Result<QList<MbRecordingHit>> parseRecordingSearch(const QByteArray &json)
{
    if (json.isEmpty()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Empty response body"),
            .detail = { },
        };
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Invalid JSON object in recording search response"),
            .detail = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Top-level is not an object"),
        };
    }

    const QJsonObject rootObj = doc.object();
    if (!rootObj.contains(QStringLiteral("recordings"))
        || !rootObj.value(QStringLiteral("recordings")).isArray()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Missing 'recordings' array in recording search response"),
            .detail = { },
        };
    }

    const QJsonArray recsArr = rootObj.value(QStringLiteral("recordings")).toArray();
    QList<MbRecordingHit> hits;
    hits.reserve(recsArr.size());

    for (const auto &recVal : recsArr) {
        if (!recVal.isObject()) {
            continue;
        }
        const QJsonObject recObj = recVal.toObject();
        MbRecordingHit hit;
        hit.id = recObj.value(QStringLiteral("id")).toString();
        hit.score = recObj.value(QStringLiteral("score")).toInt(0);
        hit.title = recObj.value(QStringLiteral("title")).toString();
        hit.artist = joinArtistCredit(recObj.value(QStringLiteral("artist-credit")));
        hit.lengthMs = recObj.value(QStringLiteral("length")).toInteger(0);

        if (recObj.contains(QStringLiteral("releases"))
            && recObj.value(QStringLiteral("releases")).isArray()) {
            const QJsonArray releasesArr = recObj.value(QStringLiteral("releases")).toArray();
            hit.releases.reserve(releasesArr.size());
            for (const auto &relVal : releasesArr) {
                if (!relVal.isObject()) {
                    continue;
                }
                const QJsonObject relObj = relVal.toObject();
                MbReleaseRef relRef;
                relRef.id = relObj.value(QStringLiteral("id")).toString();
                relRef.title = relObj.value(QStringLiteral("title")).toString();
                relRef.date = relObj.value(QStringLiteral("date")).toString();
                hit.releases.append(std::move(relRef));
            }
        }

        hits.append(std::move(hit));
    }

    return hits;
}

std::optional<int> yearFromDate(const QString &date)
{
    const QString trimmed = date.trimmed();
    static const QRegularExpression s_dateRegex(
        QStringLiteral(R"(^(\d{4})(?:-\d{2}(?:-\d{2})?)?$)"));
    const auto match = s_dateRegex.match(trimmed);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const int year = match.captured(1).toInt(&ok);
    if (!ok || year <= 0) {
        return std::nullopt;
    }
    return year;
}

QString luceneEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() * 2);
    const qsizetype len = text.size();
    for (qsizetype i = 0; i < len; ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('&') && i + 1 < len && text.at(i + 1) == QLatin1Char('&')) {
            out.append(QLatin1StringView("\\&\\&"));
            ++i;
            continue;
        }
        if (ch == QLatin1Char('|') && i + 1 < len && text.at(i + 1) == QLatin1Char('|')) {
            out.append(QLatin1StringView("\\|\\|"));
            ++i;
            continue;
        }
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('+') || ch == QLatin1Char('-')
            || ch == QLatin1Char('!') || ch == QLatin1Char('(') || ch == QLatin1Char(')')
            || ch == QLatin1Char('{') || ch == QLatin1Char('}') || ch == QLatin1Char('[')
            || ch == QLatin1Char(']') || ch == QLatin1Char('^') || ch == QLatin1Char('"')
            || ch == QLatin1Char('~') || ch == QLatin1Char('*') || ch == QLatin1Char('?')
            || ch == QLatin1Char(':') || ch == QLatin1Char('/')) {
            out.append(QLatin1Char('\\'));
        }
        out.append(ch);
    }
    return out;
}

QUrl releaseSearchUrl(const QString &title, const QString &artist, int limit)
{
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release"));
    QUrlQuery query;
    QString queryStr = QStringLiteral("release:\"%1\"").arg(luceneEscape(title));
    if (!artist.isEmpty()) {
        queryStr += QStringLiteral(" AND artist:\"%1\"").arg(luceneEscape(artist));
    }
    query.addQueryItem(QStringLiteral("query"), queryStr);
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    url.setQuery(query);
    return url;
}

QUrl releaseUrl(const QString &releaseId)
{
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release/%1").arg(releaseId));
    QUrlQuery query;
    query.addQueryItem(
        QStringLiteral("inc"), QStringLiteral("recordings+artist-credits+labels+release-groups"));
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    url.setQuery(query);
    return url;
}

QUrl recordingSearchUrl(const QString &title, const QString &artist, int limit)
{
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/recording"));
    QUrlQuery query;
    QString queryStr = QStringLiteral("recording:\"%1\"").arg(luceneEscape(title));
    if (!artist.isEmpty()) {
        queryStr += QStringLiteral(" AND artist:\"%1\"").arg(luceneEscape(artist));
    }
    query.addQueryItem(QStringLiteral("query"), queryStr);
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    url.setQuery(query);
    return url;
}

} // namespace linernotes::butler
