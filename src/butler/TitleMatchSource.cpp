// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QChar>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/ArtistName.h>
#include <butler/DuplicateFinder.h>
#include <butler/Errors.h>
#include <butler/TitleMatchSource.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace linernotes::butler {

bool isLatinOnlyTitle(const QString &title)
{
    const auto ucs4 = title.toUcs4();
    return std::ranges::all_of(ucs4, [](const uint cp) {
        const auto s = QChar::script(cp);
        return s != QChar::Script_Han && s != QChar::Script_Hiragana && s != QChar::Script_Katakana
            && s != QChar::Script_Hangul;
    });
}

namespace {

std::optional<TitlePair> createTitlePairCandidate(
    const TitleMatchTrack &t1, const TitleMatchTrack &t2)
{
    if (t1.workId == t2.workId || t1.versionType != t2.versionType) {
        return std::nullopt;
    }
    const bool latin1 = isLatinOnlyTitle(t1.baseTitle);
    const bool latin2 = isLatinOnlyTitle(t2.baseTitle);
    if (latin1 == latin2) {
        return std::nullopt;
    }

    const QString k1 = exactKey(t1.baseTitle);
    const QString k2 = exactKey(t2.baseTitle);
    if (k1.isEmpty() || k2.isEmpty() || k1 == k2) {
        return std::nullopt;
    }

    QString titleA = t1.baseTitle;
    QString titleB = t2.baseTitle;
    QString keyA = k1;
    QString keyB = k2;
    if (keyA > keyB) {
        std::swap(titleA, titleB);
        std::swap(keyA, keyB);
    }

    return TitlePair {
        .titleA = titleA,
        .titleB = titleB,
        .keyA = keyA,
        .keyB = keyB,
        .artist = t1.artistName.isEmpty() ? t2.artistName : t1.artistName,
    };
}

QHash<qint64, QList<TitleMatchTrack>> groupTracksByArtist(const QList<TitleMatchTrack> &tracks)
{
    QHash<qint64, QList<TitleMatchTrack>> artistGroups;
    for (const auto &track : tracks) {
        if (track.mainArtistId == 0) {
            continue;
        }
        auto it = artistGroups.find(track.mainArtistId);
        if (it == artistGroups.end()) {
            artistGroups.insert(track.mainArtistId, { track });
        } else {
            it.value().append(track);
        }
    }
    return artistGroups;
}

} // namespace

QList<TitlePair> findTitlePairs(const QList<TitleMatchTrack> &tracks)
{
    auto artistGroups = groupTracksByArtist(tracks);
    QMap<QPair<QString, QString>, TitlePair> pairMap;

    for (auto it = artistGroups.begin(); it != artistGroups.end(); ++it) {
        auto &group = it.value();
        std::ranges::sort(group, [](const TitleMatchTrack &a, const TitleMatchTrack &b) {
            if (a.durationMs != b.durationMs) {
                return a.durationMs < b.durationMs;
            }
            return a.trackId < b.trackId;
        });

        for (qsizetype i = 0; i < group.size(); ++i) {
            const auto &t1 = group.at(i);
            for (qsizetype j = i + 1; j < group.size(); ++j) {
                const auto &t2 = group.at(j);
                if (t2.durationMs - t1.durationMs > kDurationToleranceMs) {
                    break;
                }
                const auto pairOpt = createTitlePairCandidate(t1, t2);
                if (!pairOpt.has_value()) {
                    continue;
                }
                const auto keyPair = qMakePair(pairOpt->keyA, pairOpt->keyB);
                if (!pairMap.contains(keyPair)) {
                    pairMap.insert(keyPair, *pairOpt);
                }
            }
        }
    }

    return pairMap.values();
}

core::Result<QList<TitlePair>> parseTitlePairItemKey(const QString &itemKey)
{
    QJsonParseError parseErr { };
    const auto doc = QJsonDocument::fromJson(itemKey.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kTitleMatchInvalidKey),
            .message = QStringLiteral("Failed to parse item key as JSON object"),
            .detail = itemKey,
        };
    }

    const QJsonObject obj = doc.object();
    const QJsonValue itemsVal = obj.value(QStringLiteral("items"));
    if (!itemsVal.isArray()) {
        return core::Error {
            .code = QString(errc::kTitleMatchInvalidKey),
            .message = QStringLiteral("Missing or invalid 'items' array in item key"),
            .detail = itemKey,
        };
    }

    QList<TitlePair> pairs;
    const QJsonArray arr = itemsVal.toArray();
    pairs.reserve(arr.size());

    for (const auto &elem : arr) {
        if (!elem.isObject()) {
            return core::Error {
                .code = QString(errc::kTitleMatchInvalidKey),
                .message = QStringLiteral("Item in 'items' array is not an object"),
                .detail = itemKey,
            };
        }
        const QJsonObject itemObj = elem.toObject();
        QString titleA = itemObj.value(QStringLiteral("a")).toString();
        QString titleB = itemObj.value(QStringLiteral("b")).toString();
        const QString artist = itemObj.value(QStringLiteral("artist")).toString();

        QString keyA = exactKey(titleA);
        QString keyB = exactKey(titleB);
        if (keyA > keyB) {
            std::swap(titleA, titleB);
            std::swap(keyA, keyB);
        }

        pairs.append(TitlePair {
            .titleA = titleA,
            .titleB = titleB,
            .keyA = keyA,
            .keyB = keyB,
            .artist = artist,
        });
    }

    return pairs;
}

TitleMatchSource::TitleMatchSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<TitlePair>> TitleMatchSource::collectPendingPairs(int promptVersion) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery qKnown(conn);
    qKnown.prepare(
        QStringLiteral("SELECT key_a, key_b FROM title_matches WHERE prompt_version = ?"));
    qKnown.addBindValue(promptVersion);
    if (!qKnown.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qKnown.lastError().text(),
            .detail = QString(),
        };
    }

    QSet<QPair<QString, QString>> knownPairs;
    while (qKnown.next()) {
        knownPairs.insert(qMakePair(qKnown.value(0).toString(), qKnown.value(1).toString()));
    }

    QSqlQuery qTracks(conn);
    const QString sql = QStringLiteral("SELECT "
                                       "  t.id, "
                                       "  COALESCE(t.work_id, 0), "
                                       "  COALESCE(ta.artist_id, 0), "
                                       "  COALESCE(ar.name, ''), "
                                       "  COALESCE(w.title, ''), "
                                       "  COALESCE(tv.version_type, ''), "
                                       "  COALESCE(f.duration_ms, 0) "
                                       "FROM tracks t "
                                       "JOIN files f ON f.id = t.file_id "
                                       "LEFT JOIN works w ON w.id = t.work_id "
                                       "LEFT JOIN track_versions tv ON tv.track_id = t.id "
                                       "LEFT JOIN track_artists ta ON ta.track_id = t.id AND "
                                       "ta.role = 'artist' AND ta.position = 0 "
                                       "LEFT JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE f.missing_since IS NULL "
                                       "  AND t.cue_index IS NULL "
                                       "ORDER BY t.id ASC;");

    if (!qTracks.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qTracks.lastError().text(),
            .detail = QString(),
        };
    }

    QList<TitleMatchTrack> tracks;
    while (qTracks.next()) {
        const qint64 trackId = qTracks.value(0).toLongLong();
        if (!tracks.isEmpty() && tracks.last().trackId == trackId) {
            continue;
        }
        tracks.append(TitleMatchTrack {
            .trackId = trackId,
            .workId = qTracks.value(1).toLongLong(),
            .mainArtistId = qTracks.value(2).toLongLong(),
            .artistName = qTracks.value(3).toString(),
            .baseTitle = qTracks.value(4).toString(),
            .versionType = qTracks.value(5).toString(),
            .durationMs = qTracks.value(6).toLongLong(),
        });
    }

    const auto allPairs = findTitlePairs(tracks);
    QList<TitlePair> pendingPairs;
    pendingPairs.reserve(allPairs.size());

    for (const auto &p : allPairs) {
        if (!knownPairs.contains(qMakePair(p.keyA, p.keyB))) {
            pendingPairs.append(p);
        }
    }

    return pendingPairs;
}

core::Result<int> TitleMatchSource::countPending(int promptVersion) const
{
    auto pairsRes = collectPendingPairs(promptVersion);
    if (!pairsRes.ok()) {
        return pairsRes.error();
    }
    return static_cast<int>(pairsRes.value().size());
}

core::Result<QStringList> TitleMatchSource::findItems(int promptVersion) const
{
    auto pairsRes = collectPendingPairs(promptVersion);
    if (!pairsRes.ok()) {
        return pairsRes.error();
    }

    const auto &pairs = pairsRes.value();
    if (pairs.isEmpty()) {
        return QStringList();
    }

    QStringList groupKeys;
    for (qsizetype i = 0; i < pairs.size(); i += kBatchSize) {
        const qsizetype end = std::min(i + kBatchSize, pairs.size());
        QJsonArray arr;
        for (qsizetype j = i; j < end; ++j) {
            const auto &p = pairs.at(j);
            QJsonObject itemObj;
            itemObj.insert(QStringLiteral("a"), p.titleA);
            itemObj.insert(QStringLiteral("b"), p.titleB);
            itemObj.insert(QStringLiteral("artist"), p.artist);
            arr.append(itemObj);
        }
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("items"), arr);
        groupKeys.append(QString::fromUtf8(QJsonDocument(rootObj).toJson(QJsonDocument::Compact)));
    }

    return groupKeys;
}

} // namespace linernotes::butler
