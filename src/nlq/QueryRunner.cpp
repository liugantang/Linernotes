// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "QueryRunner.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Errors.h>
#include <library/SmartRuleSql.h>
#include <nlq/Errors.h>

namespace linernotes::nlq {

namespace {

QString buildTrackOrderSql(SortKey key, bool isAsc, const library::SmartRule &rule,
    const library::PlayCountRule &countRule, QList<QVariant> &orderBinds)
{
    const QString dir = isAsc ? QStringLiteral("ASC") : QStringLiteral("DESC");
    switch (key) {
    case SortKey::Default:
        return QStringLiteral("ts.title ASC NULLS LAST, ts.track_id ASC");
    case SortKey::PlayCount: {
        const QString expr = library::detail::buildPlayCountSqlExpr(rule, countRule, orderBinds);
        return QStringLiteral("%1 %2 NULLS LAST, ts.track_id ASC").arg(expr, dir);
    }
    case SortKey::LastPlayed:
        return QStringLiteral(
            "(SELECT tps.last_played_at FROM track_play_stats tps WHERE tps.track_id = "
            "ts.track_id) %1 NULLS LAST, ts.track_id ASC")
            .arg(dir);
    case SortKey::Rating:
        return QStringLiteral(
            "(SELECT r.rating FROM ratings r WHERE r.track_id = ts.track_id) %1 NULLS LAST, "
            "ts.track_id ASC")
            .arg(dir);
    case SortKey::Year:
        return QStringLiteral("ts.year %1 NULLS LAST, ts.track_id ASC").arg(dir);
    case SortKey::DateAdded:
        return QStringLiteral("ts.added_at %1 NULLS LAST, ts.track_id ASC").arg(dir);
    case SortKey::Duration:
        return QStringLiteral("ts.duration_ms %1 NULLS LAST, ts.track_id ASC").arg(dir);
    case SortKey::Random:
        return QStringLiteral("RANDOM(), ts.track_id ASC");
    }
    return QStringLiteral("ts.title ASC NULLS LAST, ts.track_id ASC");
}

QString buildAlbumOrderSql(SortKey key, bool isAsc, const library::SmartRule &rule,
    const library::PlayCountRule &countRule, QList<QVariant> &orderBinds)
{
    const QString dir = isAsc ? QStringLiteral("ASC") : QStringLiteral("DESC");
    switch (key) {
    case SortKey::Default:
        return QStringLiteral("MIN(ts.album) ASC NULLS LAST, ts.album_id ASC");
    case SortKey::PlayCount: {
        const QString expr = library::detail::buildPlayCountSqlExpr(rule, countRule, orderBinds);
        return QStringLiteral("SUM(%1) %2 NULLS LAST, ts.album_id ASC").arg(expr, dir);
    }
    case SortKey::LastPlayed:
        return QStringLiteral(
            "MAX((SELECT tps.last_played_at FROM track_play_stats tps WHERE tps.track_id = "
            "ts.track_id)) %1 NULLS LAST, ts.album_id ASC")
            .arg(dir);
    case SortKey::Rating:
        return QStringLiteral(
            "AVG((SELECT r.rating FROM ratings r WHERE r.track_id = ts.track_id)) %1 NULLS LAST, "
            "ts.album_id ASC")
            .arg(dir);
    case SortKey::Year:
        return QStringLiteral("MIN(ts.year) %1 NULLS LAST, ts.album_id ASC").arg(dir);
    case SortKey::DateAdded:
        return QStringLiteral("MAX(ts.added_at) %1 NULLS LAST, ts.album_id ASC").arg(dir);
    case SortKey::Duration:
        return QStringLiteral("SUM(ts.duration_ms) %1 NULLS LAST, ts.album_id ASC").arg(dir);
    case SortKey::Random:
        return QStringLiteral("RANDOM(), ts.album_id ASC");
    }
    return QStringLiteral("MIN(ts.album) ASC NULLS LAST, ts.album_id ASC");
}

QString buildArtistOrderSql(SortKey key, bool isAsc, const library::SmartRule &rule,
    const library::PlayCountRule &countRule, QList<QVariant> &orderBinds)
{
    const QString dir = isAsc ? QStringLiteral("ASC") : QStringLiteral("DESC");
    switch (key) {
    case SortKey::Default:
        return QStringLiteral(
            "(SELECT ar.name FROM artists ar WHERE ar.id = ta.artist_id) ASC NULLS LAST, "
            "ta.artist_id ASC");
    case SortKey::PlayCount: {
        const QString expr = library::detail::buildPlayCountSqlExpr(rule, countRule, orderBinds);
        return QStringLiteral("SUM(%1) %2 NULLS LAST, ta.artist_id ASC").arg(expr, dir);
    }
    case SortKey::LastPlayed:
        return QStringLiteral(
            "MAX((SELECT tps.last_played_at FROM track_play_stats tps WHERE tps.track_id = "
            "ts.track_id)) %1 NULLS LAST, ta.artist_id ASC")
            .arg(dir);
    case SortKey::Rating:
        return QStringLiteral(
            "AVG((SELECT r.rating FROM ratings r WHERE r.track_id = ts.track_id)) %1 NULLS LAST, "
            "ta.artist_id ASC")
            .arg(dir);
    case SortKey::Year:
        return QStringLiteral("MIN(ts.year) %1 NULLS LAST, ta.artist_id ASC").arg(dir);
    case SortKey::DateAdded:
        return QStringLiteral("MAX(ts.added_at) %1 NULLS LAST, ta.artist_id ASC").arg(dir);
    case SortKey::Duration:
        return QStringLiteral("SUM(ts.duration_ms) %1 NULLS LAST, ta.artist_id ASC").arg(dir);
    case SortKey::Random:
        return QStringLiteral("RANDOM(), ta.artist_id ASC");
    }
    return QStringLiteral(
        "(SELECT ar.name FROM artists ar WHERE ar.id = ta.artist_id) ASC NULLS LAST, ta.artist_id "
        "ASC");
}

QString buildQuerySql(
    const Query &query, const library::PlayCountRule &countRule, QList<QVariant> &binds)
{
    QList<QVariant> whereBinds;
    const QString whereSql
        = library::detail::buildSmartRuleWhereSql(query.rule, whereBinds, countRule);

    QList<QVariant> orderBinds;
    const bool isAsc = (query.sortOrder == Qt::AscendingOrder);

    QString sql;
    switch (query.entity) {
    case Entity::Track: {
        const QString orderSql
            = buildTrackOrderSql(query.sortKey, isAsc, query.rule, countRule, orderBinds);
        sql = QStringLiteral(
            "SELECT ts.track_id FROM track_sort ts WHERE ts.visible = 1%1 ORDER BY %2 LIMIT ?")
                  .arg(whereSql, orderSql);
        break;
    }
    case Entity::Album: {
        const QString orderSql
            = buildAlbumOrderSql(query.sortKey, isAsc, query.rule, countRule, orderBinds);
        sql = QStringLiteral("SELECT ts.album_id FROM track_sort ts WHERE ts.visible = 1 AND "
                             "ts.album_id IS NOT NULL%1 GROUP BY ts.album_id ORDER BY %2 LIMIT ?")
                  .arg(whereSql, orderSql);
        break;
    }
    case Entity::Artist: {
        const QString orderSql
            = buildArtistOrderSql(query.sortKey, isAsc, query.rule, countRule, orderBinds);
        sql = QStringLiteral("SELECT ta.artist_id FROM track_sort ts JOIN track_artists ta ON "
                             "ta.track_id = ts.track_id AND ta.role = 'artist' WHERE ts.visible = "
                             "1%1 GROUP BY ta.artist_id ORDER BY %2 LIMIT ?")
                  .arg(whereSql, orderSql);
        break;
    }
    }

    binds.append(whereBinds);
    binds.append(orderBinds);
    binds.append(query.limit);

    return sql;
}

} // namespace

QueryRunner::QueryRunner(library::Database &db, library::PlayCountRule countRule)
    : m_db(db)
    , m_countRule(countRule)
{
}

core::Result<QList<qint64>> QueryRunner::run(const Query &query) const
{
    const auto valRes = query.validate();
    if (!valRes.ok()) {
        return valRes.error();
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const QSqlDatabase &db = connRes.value();

    QList<QVariant> binds;
    const QString sql = buildQuerySql(query, m_countRule, binds);

    QSqlQuery q(db);
    q.prepare(sql);
    for (int i = 0; i < binds.size(); ++i) {
        q.bindValue(i, binds.at(i));
    }

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("NLQ query execution failed"),
            .detail = q.lastError().text(),
        };
    }

    QList<qint64> results;
    results.reserve(query.limit);
    while (q.next()) {
        results.append(q.value(0).toLongLong());
    }

    return results;
}

} // namespace linernotes::nlq
