// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EntityResolver.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <library/Errors.h>
#include <library/SmartRule.h>
#include <library/SmartRuleSql.h>

#include <iterator>

namespace linernotes::nlq {

namespace {

constexpr int kMaxCandidates = 8;

core::Result<QList<ArtistCandidate>> queryExactArtists(
    const QSqlDatabase &db, const QString &mention)
{
    static const QString s_sql
        = QStringLiteral("SELECT ar.id, ar.name, COUNT(DISTINCT ts.track_id) AS track_count "
                         "FROM artists ar "
                         "JOIN track_artists ta ON ta.artist_id = ar.id AND ta.role IN ('artist', "
                         "'featured', 'performer') "
                         "JOIN track_sort ts ON ta.track_id = ts.track_id AND ts.visible = 1 "
                         "WHERE (ar.name = ? COLLATE NOCASE OR EXISTS ("
                         "  SELECT 1 FROM artist_aliases aa "
                         "  WHERE aa.artist_id = ar.id AND aa.alias = ? COLLATE NOCASE"
                         ")) "
                         "GROUP BY ar.id, ar.name "
                         "ORDER BY track_count DESC, ar.id ASC;");

    QSqlQuery q(db);
    q.prepare(s_sql);
    q.addBindValue(mention);
    q.addBindValue(mention);

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query exact artist candidates"),
            .detail = q.lastError().text(),
        };
    }

    QList<ArtistCandidate> results;
    while (q.next()) {
        ArtistCandidate c;
        c.artistId = q.value(0).toLongLong();
        c.name = q.value(1).toString();
        c.trackCount = q.value(2).toInt();
        results.append(c);
    }
    return results;
}

core::Result<QList<ArtistCandidate>> queryFuzzyArtists(
    const QSqlDatabase &db, const QString &mention)
{
    if (mention.trimmed().isEmpty()) {
        return QList<ArtistCandidate> { };
    }

    static const QString s_sql
        = QStringLiteral("SELECT ar.id, ar.name, COUNT(DISTINCT ts.track_id) AS track_count "
                         "FROM artists ar "
                         "JOIN track_artists ta ON ta.artist_id = ar.id AND ta.role IN ('artist', "
                         "'featured', 'performer') "
                         "JOIN track_sort ts ON ta.track_id = ts.track_id AND ts.visible = 1 "
                         "WHERE (ar.name LIKE ? ESCAPE '\\' OR EXISTS ("
                         "  SELECT 1 FROM artist_aliases aa "
                         "  WHERE aa.artist_id = ar.id AND aa.alias LIKE ? ESCAPE '\\'"
                         ")) "
                         "GROUP BY ar.id, ar.name "
                         "ORDER BY track_count DESC, ar.id ASC "
                         "LIMIT 8;");

    const QString escaped = library::detail::escapeLikePattern(mention);
    const QString pattern = QStringLiteral("%%1%").arg(escaped);

    QSqlQuery q(db);
    q.prepare(s_sql);
    q.addBindValue(pattern);
    q.addBindValue(pattern);

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Failed to query fuzzy artist candidates"),
            .detail = q.lastError().text(),
        };
    }

    QList<ArtistCandidate> results;
    while (q.next()) {
        ArtistCandidate c;
        c.artistId = q.value(0).toLongLong();
        c.name = q.value(1).toString();
        c.trackCount = q.value(2).toInt();
        results.append(c);
    }
    return results;
}

} // namespace

core::Result<QList<ArtistCandidate>> findArtistCandidates(
    library::Database &db, const QString &mention)
{
    if (!db.isOpen()) {
        return core::Error {
            .code = QString(library::errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const QSqlDatabase &qDb = connRes.value();

    // 1. Exact match
    auto exactRes = queryExactArtists(qDb, mention);
    if (!exactRes.ok()) {
        return exactRes.error();
    }
    if (!exactRes.value().isEmpty()) {
        return exactRes.value();
    }

    // 2. Fuzzy match
    return queryFuzzyArtists(qDb, mention);
}

core::Result<Resolution> resolveArtists(library::Database &db, const Query &query)
{
    if (!db.isOpen()) {
        return core::Error {
            .code = QString(library::errc::kDbOpen),
            .message = QStringLiteral("Database is not open"),
            .detail = QString(),
        };
    }

    Resolution resolution;
    resolution.query = query;

    for (int i = 0; i < resolution.query.rule.conditions.size(); ++i) {
        auto &cond = *std::next(resolution.query.rule.conditions.begin(), i);
        if (cond.field != library::SmartField::Artist) {
            continue;
        }
        if (cond.op != library::SmartOp::Is && cond.op != library::SmartOp::Contains) {
            continue;
        }
        if (library::isSmartTextList(cond.value)) {
            continue;
        }

        const QString mention = cond.value.toString();
        auto candRes = findArtistCandidates(db, mention);
        if (!candRes.ok()) {
            return candRes.error();
        }

        auto candidates = candRes.value();
        if (candidates.size() == 1) {
            cond.op = library::SmartOp::Is;
            cond.value = candidates.at(0).name;
        } else if (candidates.size() > 1) {
            if (candidates.size() > kMaxCandidates) {
                candidates = candidates.mid(0, kMaxCandidates);
            }
            Clarification clarification;
            clarification.conditionIndex = i;
            clarification.mention = mention;
            clarification.candidates = candidates;
            resolution.clarifications.append(clarification);
        }
        // 0 candidates: keep original condition
    }

    return resolution;
}

Query applyArtistChoice(const Query &query, int conditionIndex, const ArtistCandidate &choice)
{
    Query updated = query;
    if (conditionIndex >= 0 && conditionIndex < updated.rule.conditions.size()) {
        auto &cond = *std::next(updated.rule.conditions.begin(), conditionIndex);
        cond.field = library::SmartField::Artist;
        cond.op = library::SmartOp::Is;
        cond.value = choice.name;
        cond.value2 = QVariant();
    }
    return updated;
}

} // namespace linernotes::nlq
