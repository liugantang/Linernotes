// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCreditSource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <butler/ArtistCredit.h>
#include <butler/ArtistCreditStore.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>

namespace linernotes::butler {

namespace {

struct SkippedTargets {
    QSet<QPair<qint64, QString>> pendingOrOverride;
    QSet<QPair<QPair<qint64, QString>, QString>> rejected;
};

inline bool isSkipped(
    const SkippedTargets &skipped, qint64 trackId, const QString &field, const QString &value)
{
    if (skipped.pendingOrOverride.contains(qMakePair(trackId, field))) {
        return true;
    }
    if (skipped.rejected.contains(qMakePair(qMakePair(trackId, field), value))) {
        return true;
    }
    return false;
}

core::Result<SkippedTargets> fetchAllSkipped(const QSqlDatabase &conn)
{
    SkippedTargets skipped;

    QSqlQuery qCorr(conn);
    if (!qCorr.exec(QStringLiteral("SELECT entity_id, field FROM corrections "
                                   "WHERE entity_type = 'track' AND status = 'pending'"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qCorr.lastError().text(),
            .detail = QString(),
        };
    }
    while (qCorr.next()) {
        skipped.pendingOrOverride.insert(
            qMakePair(qCorr.value(0).toLongLong(), qCorr.value(1).toString()));
    }

    QSqlQuery qOver(conn);
    if (!qOver.exec(QStringLiteral("SELECT track_id, field FROM user_overrides"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qOver.lastError().text(),
            .detail = QString(),
        };
    }
    while (qOver.next()) {
        skipped.pendingOrOverride.insert(
            qMakePair(qOver.value(0).toLongLong(), qOver.value(1).toString()));
    }

    QSqlQuery qRej(conn);
    if (!qRej.exec(QStringLiteral("SELECT c.entity_id, c.field, c.old_value "
                                  "FROM corrections c "
                                  "JOIN correction_batches cb ON cb.id = c.batch_id "
                                  "WHERE c.entity_type = 'track' "
                                  "  AND c.status = 'rejected' "
                                  "  AND cb.kind = 'artist_credit'"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = qRej.lastError().text(),
            .detail = QString(),
        };
    }
    while (qRej.next()) {
        const qint64 entityId = qRej.value(0).toLongLong();
        const QString field = qRej.value(1).toString();
        const QString oldValue = qRej.value(2).toString();
        skipped.rejected.insert(qMakePair(qMakePair(entityId, field), oldValue));
    }

    return skipped;
}

core::Result<QList<TrackFieldTarget>> queryTargetsForValue(
    const QSqlDatabase &conn, const SkippedTargets &skipped, const QString &trimmedVal)
{
    if (trimmedVal.isEmpty()) {
        return QList<TrackFieldTarget>();
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT t.id, em.artist, em.album_artist "
                             "FROM tracks t "
                             "JOIN effective_metadata em ON em.track_id = t.id "
                             "WHERE em.artist = ? OR em.album_artist = ? "
                             "ORDER BY t.id ASC"));
    q.addBindValue(trimmedVal);
    q.addBindValue(trimmedVal);

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = trimmedVal,
        };
    }

    QList<TrackFieldTarget> targets;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QString artistVal = q.value(1).toString().trimmed();
        const QString albumArtistVal = q.value(2).toString().trimmed();

        if (artistVal == trimmedVal
            && !isSkipped(skipped, trackId, QStringLiteral("artist"), trimmedVal)) {
            targets.append(TrackFieldTarget {
                .trackId = trackId,
                .field = library::TagField::Artist,
            });
        }
        if (albumArtistVal == trimmedVal
            && !isSkipped(skipped, trackId, QStringLiteral("album_artist"), trimmedVal)) {
            targets.append(TrackFieldTarget {
                .trackId = trackId,
                .field = library::TagField::AlbumArtist,
            });
        }
    }

    return targets;
}

QStringList chunkValuesIntoKeys(
    const QString &type, const QSet<QString> &valuesSet, qsizetype chunkSize = 100)
{
    QStringList sortedList = valuesSet.values();
    std::ranges::sort(sortedList);

    QStringList groupKeys;
    for (qsizetype i = 0; i < sortedList.size(); i += chunkSize) {
        const qsizetype end = std::min(i + chunkSize, sortedList.size());
        QJsonArray arr;
        for (qsizetype j = i; j < end; ++j) {
            arr.append(sortedList.at(j));
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("type"), type);
        obj.insert(QStringLiteral("values"), arr);
        groupKeys.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
    return groupKeys;
}

} // namespace

ArtistCreditSource::ArtistCreditSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<TrackFieldTarget>> ArtistCreditSource::targetsFor(const QString &value) const
{
    const QString trimmedVal = value.trimmed();
    if (trimmedVal.isEmpty()) {
        return QList<TrackFieldTarget>();
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    auto skippedRes = fetchAllSkipped(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }

    return queryTargetsForValue(conn, skippedRes.value(), trimmedVal);
}

core::Result<QStringList> ArtistCreditSource::findItems(int promptVersion) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const core::SystemClock clock;
    const ArtistCreditStore store(m_db, clock);
    auto cacheRes = store.loadAll();
    if (!cacheRes.ok()) {
        return cacheRes.error();
    }
    const auto &cache = cacheRes.value();

    auto skippedRes = fetchAllSkipped(conn);
    if (!skippedRes.ok()) {
        return skippedRes.error();
    }
    const auto &skipped = skippedRes.value();

    QSqlQuery q(conn);
    const QString sql = QStringLiteral("SELECT DISTINCT artist FROM effective_metadata "
                                       "WHERE artist IS NOT NULL AND artist != '' "
                                       "UNION "
                                       "SELECT DISTINCT album_artist FROM effective_metadata "
                                       "WHERE album_artist IS NOT NULL AND album_artist != ''");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QSet<QString> unparsedSet;
    QSet<QString> cachedSet;

    while (q.next()) {
        const QString val = q.value(0).toString().trimmed();
        if (val.isEmpty()) {
            continue;
        }

        if (!cache.contains(val)) {
            unparsedSet.insert(val);
            continue;
        }

        const auto &entry = cache.value(val);
        if (entry.promptVersion < promptVersion) {
            unparsedSet.insert(val);
            continue;
        }

        const QString norm = normalizedValue(entry.credit);
        if (norm != val) {
            auto targetsRes = queryTargetsForValue(conn, skipped, val);
            if (targetsRes.ok() && !targetsRes.value().isEmpty()) {
                cachedSet.insert(val);
            }
        }
    }

    QStringList groupKeys;
    groupKeys.append(chunkValuesIntoKeys(QStringLiteral("cached"), cachedSet));
    groupKeys.append(chunkValuesIntoKeys(QStringLiteral("parse"), unparsedSet));

    return groupKeys;
}

} // namespace linernotes::butler
