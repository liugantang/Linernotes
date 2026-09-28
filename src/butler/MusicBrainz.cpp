// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MusicBrainz.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUrlQuery>

#include <butler/Errors.h>

namespace linernotes::butler {

namespace {

QString escapeLucene(const QString &input)
{
    QString out;
    out.reserve(input.size() * 2);
    const qsizetype len = input.size();
    for (qsizetype i = 0; i < len; ++i) {
        const QChar ch = input.at(i);
        if (ch == QLatin1Char('&') && i + 1 < len && input.at(i + 1) == QLatin1Char('&')) {
            out.append(QLatin1StringView("\\&\\&"));
            ++i;
            continue;
        }
        if (ch == QLatin1Char('|') && i + 1 < len && input.at(i + 1) == QLatin1Char('|')) {
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

MbAlias parseAlias(const QJsonObject &obj)
{
    MbAlias alias;
    alias.name = obj.value(QStringLiteral("name")).toString();
    alias.sortName = obj.value(QStringLiteral("sort-name")).toString();
    const QJsonValue localeVal = obj.value(QStringLiteral("locale"));
    if (localeVal.isString()) {
        alias.locale = localeVal.toString();
    }
    const QJsonValue primaryVal = obj.value(QStringLiteral("primary"));
    if (primaryVal.isBool()) {
        alias.primary = primaryVal.toBool();
    }
    return alias;
}

MbArtist parseArtist(const QJsonObject &obj)
{
    MbArtist artist;
    artist.mbid = obj.value(QStringLiteral("id")).toString();
    artist.name = obj.value(QStringLiteral("name")).toString();
    artist.sortName = obj.value(QStringLiteral("sort-name")).toString();
    artist.type = obj.value(QStringLiteral("type")).toString();
    artist.country = obj.value(QStringLiteral("country")).toString();
    artist.disambiguation = obj.value(QStringLiteral("disambiguation")).toString();

    const QJsonValue scoreVal = obj.value(QStringLiteral("score"));
    if (scoreVal.isDouble()) {
        artist.score = scoreVal.toInt();
    } else if (scoreVal.isString()) {
        artist.score = scoreVal.toString().toInt();
    }

    if (obj.contains(QStringLiteral("aliases")) && obj.value(QStringLiteral("aliases")).isArray()) {
        const QJsonArray aliasArr = obj.value(QStringLiteral("aliases")).toArray();
        artist.aliases.reserve(aliasArr.size());
        for (const auto &aliasVal : aliasArr) {
            if (aliasVal.isObject()) {
                artist.aliases.append(parseAlias(aliasVal.toObject()));
            }
        }
    }
    return artist;
}

} // namespace

core::Result<QList<MbArtist>> parseArtistSearch(const QByteArray &body)
{
    if (body.isEmpty()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Empty response body"),
            .detail = { },
        };
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Invalid JSON object in MusicBrainz response"),
            .detail = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Top-level is not an object"),
        };
    }

    const QJsonObject rootObj = doc.object();
    if (!rootObj.contains(QStringLiteral("artists"))
        || !rootObj.value(QStringLiteral("artists")).isArray()) {
        return core::Error {
            .code = QString(errc::kMbInvalidResponse),
            .message = QStringLiteral("Missing 'artists' array in MusicBrainz response"),
            .detail = { },
        };
    }

    const QJsonArray artistsArray = rootObj.value(QStringLiteral("artists")).toArray();
    QList<MbArtist> artists;
    artists.reserve(artistsArray.size());

    for (const auto &artistVal : artistsArray) {
        if (artistVal.isObject()) {
            artists.append(parseArtist(artistVal.toObject()));
        }
    }

    return artists;
}

QUrl artistSearchUrl(const QString &name, int limit)
{
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/artist/"));
    QUrlQuery query;
    const QString escaped = escapeLucene(name);
    query.addQueryItem(QStringLiteral("query"), QStringLiteral("artist:\"%1\"").arg(escaped));
    query.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(limit));
    url.setQuery(query);
    return url;
}

std::optional<QList<MbArtist>> cachedArtistSearch(
    const QSqlDatabase &db, const QString &name, qint64 nowMs)
{
    const QUrl url = artistSearchUrl(name);
    const QString urlString = url.toString();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT body, fetched_at FROM mb_cache WHERE url = ?;"));
    q.addBindValue(urlString);
    if (q.exec() && q.next()) {
        const QString body = q.value(0).toString();
        const qint64 fetchedAt = q.value(1).toLongLong();
        if (nowMs >= fetchedAt && (nowMs - fetchedAt) < kMaxCacheAgeMs) {
            auto parseRes = parseArtistSearch(body.toUtf8());
            if (parseRes.ok()) {
                return parseRes.value();
            }
        }
    }
    return std::nullopt;
}

} // namespace linernotes::butler
