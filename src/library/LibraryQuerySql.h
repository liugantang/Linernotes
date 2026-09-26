// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

/**
 * @file LibraryQuerySql.h
 * @brief SQL query construction and row parsers for LibraryQuery.
 *
 * 仅供 library 内部使用 (Internal to the library module only).
 */

#include <QHash>
#include <QList>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <Qt>

#include <core/Result.h>
#include <library/Errors.h>
#include <library/LibraryQuery.h>

#include <algorithm>

namespace linernotes::library::detail {

struct OrderClauses {
    QString pageOrder;
    QString outerOrder;
};

TrackRow parseTrackRow(const QSqlQuery &q);
AlbumRow parseAlbumRow(const QSqlQuery &q);
ArtistRow parseArtistRow(const QSqlQuery &q);

inline qint64 rowEntityId(const TrackRow &r)
{
    return r.trackId;
}
inline qint64 rowEntityId(const AlbumRow &r)
{
    return r.albumId;
}
inline qint64 rowEntityId(const ArtistRow &r)
{
    return r.artistId;
}

OrderClauses buildTrackOrderClauses(TrackSortKey key, bool isAsc);
OrderClauses buildAlbumOrderClauses(AlbumSortKey key, bool isAsc);

QString buildTrackFilterWhereSql(const TrackFilter &filter, QList<QVariant> &binds);
QString buildAlbumFilterWhereSql(const AlbumFilter &filter, QList<QVariant> &binds);
QString buildArtistFilterWhereSql(const ArtistFilter &filter);

QString trackSelectSql(const QString &fromSource = QStringLiteral("tracks t"));
QString albumSelectSql(const QString &fromSource = QStringLiteral("albums a"),
    const QString &alias = QStringLiteral("a"));
QString artistSelectSql(const QString &fromSource = QStringLiteral("artists ar"),
    const QString &alias = QStringLiteral("ar"));
QString artistVisibilityWhereSql(const QString &artistIdExpr = QStringLiteral("ar.id"));

template <typename RowT, typename ParseFn>
core::Result<QList<RowT>> fetchRowsByIds(const QSqlDatabase &db, const QList<qint64> &ids,
    const QString &sqlFormat, const QString &baseSelectSql, ParseFn parseRow,
    const QString &queryName)
{
    if (!db.isOpen()) {
        return core::Error {
            .code = QString(errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    if (ids.isEmpty()) {
        return QList<RowT> { };
    }

    QList<qint64> uniqueIds;
    uniqueIds.reserve(ids.size());
    QSet<qint64> seen;
    for (const qint64 id : ids) {
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            uniqueIds.append(id);
        }
    }

    if (uniqueIds.isEmpty()) {
        return QList<RowT> { };
    }

    QHash<qint64, RowT> rowMap;
    rowMap.reserve(uniqueIds.size());

    constexpr int kBatchSize = 500;
    for (int start = 0; start < uniqueIds.size(); start += kBatchSize) {
        const int count = std::min(kBatchSize, static_cast<int>(uniqueIds.size()) - start);
        QStringList placeholders;
        placeholders.reserve(count);
        for (int i = 0; i < count; ++i) {
            placeholders.append(QStringLiteral("?"));
        }

        const QString sql = sqlFormat.arg(baseSelectSql, placeholders.join(QStringLiteral(", ")));

        QSqlQuery q(db);
        q.prepare(sql);
        for (int i = 0; i < count; ++i) {
            q.bindValue(i, uniqueIds.at(start + i));
        }

        if (!q.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = queryName + QStringLiteral(" query failed"),
                .detail = q.lastError().text(),
            };
        }

        while (q.next()) {
            const RowT row = parseRow(q);
            rowMap.insert(rowEntityId(row), row);
        }
    }

    QList<RowT> result;
    result.reserve(ids.size());
    for (const qint64 id : ids) {
        const auto it = rowMap.constFind(id);
        if (it != rowMap.constEnd()) {
            result.append(*it);
        }
    }

    return result;
}

} // namespace linernotes::library::detail
