// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ItunesSearch.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>

#include <butler/Errors.h>
#include <unicode/translit.h>
#include <unicode/uchar.h>
#include <unicode/unistr.h>
#include <unicode/uscript.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

namespace linernotes::butler {

namespace {

icu::Transliterator *getHansToHant()
{
    thread_local const std::unique_ptr<icu::Transliterator> s_trans = []() {
        UErrorCode status = U_ZERO_ERROR;
        auto *t = icu::Transliterator::createInstance(
            icu::UnicodeString::fromUTF8("Hans-Hant"), UTRANS_FORWARD, status);
        return std::unique_ptr<icu::Transliterator>(t);
    }();
    return s_trans.get();
}

QString transliterateToHant(const QString &text)
{
    auto *trans = getHansToHant();
    if (trans == nullptr || text.isEmpty()) {
        return text;
    }
    icu::UnicodeString ustr(text.utf16(), static_cast<int32_t>(text.length()));
    trans->transliterate(ustr);
    return QString::fromUtf16(ustr.getBuffer(), static_cast<qsizetype>(ustr.length()));
}

bool isHan(uint cp)
{
    UErrorCode status = U_ZERO_ERROR;
    const UScriptCode sc = uscript_getScript(static_cast<UChar32>(cp), &status);
    if (status <= U_ZERO_ERROR && sc == USCRIPT_HAN) {
        return true;
    }
    if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x20000 && cp <= 0x2FA1F) || (cp >= 0xF900 && cp <= 0xFAFF)) {
        return true;
    }
    return false;
}

bool containsHan(const QString &text)
{
    const QList<uint> ucs4 = text.toUcs4();
    return std::ranges::any_of(ucs4, [](uint cp) { return isHan(cp); });
}

} // namespace

QStringList itunesSearchTerms(const QString &albumTitle, const QString &albumArtist)
{
    const QString title = albumTitle.trimmed();
    const QString artist = albumArtist.trimmed();

    QString baseTerm;
    if (artist.isEmpty()) {
        baseTerm = title;
    } else if (title.isEmpty()) {
        baseTerm = artist;
    } else {
        baseTerm = QStringLiteral("%1 %2").arg(artist, title);
    }

    if (baseTerm.isEmpty()) {
        return { };
    }

    QStringList terms;
    terms.append(baseTerm);

    if (containsHan(baseTerm)) {
        const QString hantTerm = transliterateToHant(baseTerm);
        if (hantTerm != baseTerm && !terms.contains(hantTerm)) {
            terms.append(hantTerm);
        }
    }

    return terms;
}

QUrl itunesSearchUrl(const QString &term, QStringView country)
{
    QUrl url(QStringLiteral("https://itunes.apple.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("term"), term);
    query.addQueryItem(QStringLiteral("entity"), QStringLiteral("album"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
    query.addQueryItem(QStringLiteral("country"), country.toString());
    url.setQuery(query);
    return url;
}

core::Result<QList<ItunesAlbum>> parseItunesSearch(const QByteArray &json, const QString &country)
{
    if (json.isEmpty()) {
        return core::Error {
            .code = QString(errc::kItunesParseFailed),
            .message = QStringLiteral("Empty response body"),
            .detail = { },
        };
    }

    QJsonParseError parseError { };
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kItunesParseFailed),
            .message = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("Top-level is not an object"),
            .detail = QString::fromUtf8(json),
        };
    }

    const QJsonObject rootObj = doc.object();
    const QJsonValue resultsVal = rootObj.value(QStringLiteral("results"));
    if (!resultsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kItunesParseFailed),
            .message = QStringLiteral("Missing or invalid 'results' array"),
            .detail = { },
        };
    }

    const QJsonArray resultsArr = resultsVal.toArray();
    QList<ItunesAlbum> albums;
    albums.reserve(resultsArr.size());

    for (const auto &val : resultsArr) {
        if (!val.isObject()) {
            continue;
        }
        const QJsonObject obj = val.toObject();
        ItunesAlbum album;
        album.collectionId = obj.value(QStringLiteral("collectionId")).toInteger();
        album.title = obj.value(QStringLiteral("collectionName")).toString();
        album.artist = obj.value(QStringLiteral("artistName")).toString();

        const QString dateStr = obj.value(QStringLiteral("releaseDate")).toString();
        const QDateTime dt = QDateTime::fromString(dateStr, Qt::ISODate);
        if (dt.isValid()) {
            album.year = dt.date().year();
        } else if (dateStr.size() >= 4) {
            bool ok = false;
            const int y = dateStr.first(4).toInt(&ok);
            if (ok && y > 0) {
                album.year = y;
            }
        }

        album.trackCount = obj.value(QStringLiteral("trackCount")).toInt();
        album.country = country;
        album.artworkUrl100 = QUrl(obj.value(QStringLiteral("artworkUrl100")).toString());

        albums.append(std::move(album));
    }

    return albums;
}

QUrl itunesArtworkUrl(const QUrl &artworkUrl100, int size)
{
    if (!artworkUrl100.isValid()) {
        return artworkUrl100;
    }

    const QString urlStr = artworkUrl100.toString();
    static const QRegularExpression s_pattern(QStringLiteral(R"(100x100bb(\.[a-zA-Z0-9]+)$)"));
    const auto match = s_pattern.match(urlStr);
    if (!match.hasMatch()) {
        return artworkUrl100;
    }

    const QString ext = match.captured(1);
    const QString replacement
        = QStringLiteral("%1x%2bb%3").arg(QString::number(size), QString::number(size), ext);
    QString newUrlStr = urlStr;
    newUrlStr.replace(match.capturedStart(0), match.capturedLength(0), replacement);
    return QUrl(newUrlStr);
}

QList<ItunesAlbum> mergeItunesResults(const QList<QList<ItunesAlbum>> &perRequest)
{
    QList<ItunesAlbum> merged;
    QSet<qint64> seenIds;
    for (const auto &list : perRequest) {
        for (const auto &album : list) {
            if (album.collectionId != 0 && seenIds.contains(album.collectionId)) {
                continue;
            }
            if (album.collectionId != 0) {
                seenIds.insert(album.collectionId);
            }
            merged.append(album);
        }
    }
    return merged;
}

} // namespace linernotes::butler
