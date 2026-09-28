// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistSplitSource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <butler/Errors.h>
#include <library/Database.h>

#include <algorithm>

namespace linernotes::butler {

namespace {

core::Result<QSet<QPair<qint64, QString>>> fetchSkippedFields(const QSqlDatabase &conn)
{
    QSet<QPair<qint64, QString>> skipped;

    QSqlQuery qCorr(conn);
    if (!qCorr.exec(QStringLiteral("SELECT entity_id, field FROM corrections "
                                   "WHERE entity_type = 'track' AND status = 'pending'"))) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = qCorr.lastError().text(),
            .detail = QString(),
        };
    }
    while (qCorr.next()) {
        skipped.insert(qMakePair(qCorr.value(0).toLongLong(), qCorr.value(1).toString()));
    }

    QSqlQuery qOver(conn);
    if (!qOver.exec(QStringLiteral("SELECT track_id, field FROM user_overrides"))) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = qOver.lastError().text(),
            .detail = QString(),
        };
    }
    while (qOver.next()) {
        skipped.insert(qMakePair(qOver.value(0).toLongLong(), qOver.value(1).toString()));
    }

    return skipped;
}

QSet<QString> fetchKnownArtists(const QSqlDatabase &conn)
{
    QSet<QString> known;
    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT DISTINCT artist FROM effective_metadata "
                                       "WHERE artist IS NOT NULL AND artist != '' "
                                       "UNION "
                                       "SELECT DISTINCT album_artist FROM effective_metadata "
                                       "WHERE album_artist IS NOT NULL AND album_artist != ''");

    if (q.exec(sql)) {
        while (q.next()) {
            const QString val = q.value(0).toString().trimmed();
            if (val.isEmpty()) {
                continue;
            }
            if (val.contains(QStringLiteral(" / "))) {
                const QStringList parts = val.split(QStringLiteral(" / "), Qt::SkipEmptyParts);
                for (const auto &p : parts) {
                    const QString tp = p.trimmed();
                    if (!tp.isEmpty() && !hasSeparators(tp)) {
                        known.insert(tp);
                    }
                }
            } else if (!hasSeparators(val)) {
                known.insert(val);
            }
        }
    }
    return known;
}

struct TargetInfo {
    QList<TrackFieldTarget> targets;
    QStringList contextAlbums;
};

TargetInfo queryCandidateTargets(
    const QSqlDatabase &conn, const QSet<QPair<qint64, QString>> &skipped, const QString &val)
{
    TargetInfo info;
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT t.id, em.artist, em.album_artist, em.album, em.year "
                             "FROM tracks t "
                             "JOIN effective_metadata em ON em.track_id = t.id "
                             "WHERE em.artist = ? OR em.album_artist = ? "
                             "ORDER BY t.id ASC"));
    q.addBindValue(val);
    q.addBindValue(val);

    if (!q.exec()) {
        return info;
    }

    QSet<QString> seenAlbums;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QString art = q.value(1).toString().trimmed();
        const QString albArt = q.value(2).toString().trimmed();
        const QString alb = q.value(3).toString().trimmed();
        const int year = q.value(4).toInt();

        if (art == val && !skipped.contains(qMakePair(trackId, QStringLiteral("artist")))) {
            info.targets.append(TrackFieldTarget {
                .trackId = trackId,
                .field = library::TagField::Artist,
            });
        }
        if (albArt == val
            && !skipped.contains(qMakePair(trackId, QStringLiteral("album_artist")))) {
            info.targets.append(TrackFieldTarget {
                .trackId = trackId,
                .field = library::TagField::AlbumArtist,
            });
        }

        if (!alb.isEmpty() && !seenAlbums.contains(alb) && info.contextAlbums.size() < 3) {
            seenAlbums.insert(alb);
            const QString ctx
                = year > 0 ? QStringLiteral("%1 (%2)").arg(alb).arg(QString::number(year)) : alb;
            info.contextAlbums.append(ctx);
        }
    }
    return info;
}

} // namespace

ArtistSplitSource::ArtistSplitSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QStringList> ArtistSplitSource::findItems() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    auto skippedRes = fetchSkippedFields(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }
    const auto &skipped = skippedRes.value();
    const auto known = fetchKnownArtists(conn);

    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT t.id, em.artist, em.album_artist "
                                       "FROM tracks t "
                                       "JOIN effective_metadata em ON em.track_id = t.id "
                                       "ORDER BY t.id ASC");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidResult),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QSet<QString> candidateValues;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QString artistVal = q.value(1).toString().trimmed();
        const QString albumArtistVal = q.value(2).toString().trimmed();

        if (!artistVal.isEmpty()
            && !skipped.contains(qMakePair(trackId, QStringLiteral("artist")))) {
            const auto dec = decideSplit(artistVal, known);
            if (dec.verdict == SplitVerdict::Split || dec.verdict == SplitVerdict::Ambiguous) {
                candidateValues.insert(artistVal);
            }
        }

        if (!albumArtistVal.isEmpty()
            && !skipped.contains(qMakePair(trackId, QStringLiteral("album_artist")))) {
            const auto dec = decideSplit(albumArtistVal, known);
            if (dec.verdict == SplitVerdict::Split || dec.verdict == SplitVerdict::Ambiguous) {
                candidateValues.insert(albumArtistVal);
            }
        }
    }

    QStringList sortedList = candidateValues.values();
    std::ranges::sort(sortedList);

    QStringList groupKeys;
    for (qsizetype i = 0; i < sortedList.size(); i += 20) {
        QJsonArray arr;
        const qsizetype end = std::min(i + 20, sortedList.size());
        for (qsizetype j = i; j < end; ++j) {
            arr.append(sortedList.at(j));
        }
        groupKeys.append(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }

    return groupKeys;
}

core::Result<ArtistSplitGroup> ArtistSplitSource::loadItem(const QString &key) const
{
    QJsonParseError parseErr { };
    const QJsonDocument doc = QJsonDocument::fromJson(key.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isArray()) {
        return core::Error {
            .code = QString(errc::kArtistSplitInvalidKey),
            .message = QStringLiteral("Failed to parse item key JSON array"),
            .detail = key,
        };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    auto skippedRes = fetchSkippedFields(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }
    const auto &skipped = skippedRes.value();
    const auto known = fetchKnownArtists(conn);

    ArtistSplitGroup group;
    group.key = key;

    int candId = 0;
    const QJsonArray arr = doc.array();
    for (const auto &elem : arr) {
        const QString val = elem.toString().trimmed();
        if (val.isEmpty()) {
            continue;
        }

        const auto dec = decideSplit(val, known);
        const auto targetInfo = queryCandidateTargets(conn, skipped, val);

        if (!targetInfo.targets.isEmpty()) {
            group.candidates.append(ArtistSplitCandidate {
                .id = candId++,
                .original = val,
                .decision = dec,
                .targets = targetInfo.targets,
                .contextAlbums = targetInfo.contextAlbums,
            });
        }
    }

    if (group.candidates.isEmpty()) {
        return core::Error {
            .code = QString(errc::kArtistSplitItemNotFound),
            .message = QStringLiteral("No targets found for items in key"),
            .detail = key,
        };
    }

    return group;
}

} // namespace linernotes::butler
