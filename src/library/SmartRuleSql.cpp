// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartRuleSql.h"

#include <QDate>
#include <QDateTime>
#include <QStringList>

namespace linernotes::library::detail {

QString escapeLikePattern(const QString &input)
{
    QString result;
    result.reserve(input.size() + 8);
    for (const QChar c : input) {
        if (c == u'\\' || c == u'%' || c == u'_') {
            result.append(u'\\');
        }
        result.append(c);
    }
    return result;
}

namespace {

struct PlayWindow {
    std::optional<qint64> fromMs;
    std::optional<qint64> toMs;
    [[nodiscard]] bool hasWindow() const { return fromMs.has_value() || toMs.has_value(); }
};

PlayWindow extractPlayWindow(const SmartRule &rule)
{
    PlayWindow w;
    if (rule.playedFrom.has_value()) {
        w.fromMs = rule.playedFrom->startOfDay().toMSecsSinceEpoch();
    }
    if (rule.playedTo.has_value()) {
        w.toMs = rule.playedTo->addDays(1).startOfDay().toMSecsSinceEpoch();
    }
    return w;
}

void appendWindowConditions(const PlayWindow &w, QStringList &clauses, QList<QVariant> &binds)
{
    if (w.fromMs.has_value()) {
        clauses.append(QStringLiteral("pe.started_at >= ?"));
        binds.append(*w.fromMs);
    }
    if (w.toMs.has_value()) {
        clauses.append(QStringLiteral("pe.started_at < ?"));
        binds.append(*w.toMs);
    }
}

QString buildTextConditionSql(const SmartCondition &cond, QList<QVariant> &binds)
{
    if (cond.field == SmartField::Artist && (cond.op == SmartOp::Is || cond.op == SmartOp::IsNot)) {
        const QString rawVal = cond.value.toString();
        const QString escaped = escapeLikePattern(rawVal);
        binds.append(rawVal);
        binds.append(rawVal);
        binds.append(escaped);
        QString matchSql
            = QStringLiteral("(EXISTS (SELECT 1 FROM track_artists ta "
                             "JOIN artists a ON a.id = ta.artist_id "
                             "LEFT JOIN artist_aliases aa ON aa.artist_id = a.id "
                             "WHERE ta.track_id = ts.track_id "
                             "AND ta.role IN ('artist', 'featured', 'performer') "
                             "AND (a.name = ? COLLATE NOCASE OR aa.alias = ? COLLATE NOCASE)) "
                             "OR COALESCE(ts.artist, '') LIKE ? ESCAPE '\\')");
        if (cond.op == SmartOp::Is) {
            return matchSql;
        }
        return QStringLiteral("NOT (%1)").arg(matchSql);
    }

    QString expr;
    if (cond.field == SmartField::Title) {
        expr = QStringLiteral("ts.title");
    } else if (cond.field == SmartField::Artist) {
        expr = QStringLiteral("ts.artist");
    } else if (cond.field == SmartField::Album) {
        expr = QStringLiteral("ts.album");
    } else if (cond.field == SmartField::AlbumArtist) {
        expr = QStringLiteral(
            "(SELECT em.album_artist FROM effective_metadata em WHERE em.track_id = "
            "ts.track_id)");
    } else if (cond.field == SmartField::Genre) {
        expr = QStringLiteral("ts.genre");
    } else if (cond.field == SmartField::Codec) {
        expr = QStringLiteral(
            "(SELECT f.codec FROM tracks t JOIN files f ON t.file_id = f.id WHERE t.id = "
            "ts.track_id)");
    }

    const QString escaped = escapeLikePattern(cond.value.toString());
    switch (cond.op) {
    case SmartOp::Contains:
        binds.append(QStringLiteral("%%1%").arg(escaped));
        return QStringLiteral("COALESCE(%1, '') LIKE ? ESCAPE '\\'").arg(expr);
    case SmartOp::NotContains:
        binds.append(QStringLiteral("%%1%").arg(escaped));
        return QStringLiteral("COALESCE(%1, '') NOT LIKE ? ESCAPE '\\'").arg(expr);
    case SmartOp::Is:
        binds.append(escaped);
        return QStringLiteral("COALESCE(%1, '') LIKE ? ESCAPE '\\'").arg(expr);
    case SmartOp::IsNot:
        binds.append(escaped);
        return QStringLiteral("COALESCE(%1, '') NOT LIKE ? ESCAPE '\\'").arg(expr);
    case SmartOp::StartsWith:
        binds.append(QStringLiteral("%1%").arg(escaped));
        return QStringLiteral("COALESCE(%1, '') LIKE ? ESCAPE '\\'").arg(expr);
    default:
        return { };
    }
}

} // namespace

QString buildPlayCountSqlExpr(
    const SmartRule &rule, const PlayCountRule &countRule, QList<QVariant> &binds)
{
    const PlayWindow w = extractPlayWindow(rule);
    if (w.hasWindow()) {
        QStringList peClauses;
        peClauses.append(QStringLiteral("pe.track_id = ts.track_id"));
        appendWindowConditions(w, peClauses, binds);

        const auto norm = countRule.normalized();
        if (norm.minSeconds > 0) {
            peClauses.append(
                QStringLiteral("((pe.played_ms >= ?) OR (pe.track_duration_ms IS NOT NULL AND "
                               "pe.track_duration_ms > 0 AND pe.played_ms * 100 >= ? * "
                               "pe.track_duration_ms))"));
            binds.append(static_cast<qint64>(norm.minSeconds) * 1000);
            binds.append(norm.minPercent);
        } else {
            peClauses.append(
                QStringLiteral("(pe.track_duration_ms IS NOT NULL AND pe.track_duration_ms > 0 AND "
                               "pe.played_ms * 100 >= ? * pe.track_duration_ms)"));
            binds.append(norm.minPercent);
        }
        return QStringLiteral("(SELECT COUNT(*) FROM play_events pe WHERE %1)")
            .arg(peClauses.join(QStringLiteral(" AND ")));
    }
    return QStringLiteral(
        "COALESCE((SELECT tps.play_count FROM track_play_stats tps WHERE tps.track_id = "
        "ts.track_id), 0)");
}

namespace {

QString buildSkipCountExpr(const SmartRule &rule, QList<QVariant> &binds)
{
    const PlayWindow w = extractPlayWindow(rule);
    if (w.hasWindow()) {
        QStringList peClauses;
        peClauses.append(QStringLiteral("pe.track_id = ts.track_id"));
        appendWindowConditions(w, peClauses, binds);
        peClauses.append(QStringLiteral("pe.skipped = 1"));
        return QStringLiteral("(SELECT COUNT(*) FROM play_events pe WHERE %1)")
            .arg(peClauses.join(QStringLiteral(" AND ")));
    }
    return QStringLiteral(
        "COALESCE((SELECT tps.skip_count FROM track_play_stats tps WHERE tps.track_id = "
        "ts.track_id), 0)");
}

QString buildCompletedCountExpr(const SmartRule &rule, QList<QVariant> &binds)
{
    const PlayWindow w = extractPlayWindow(rule);
    if (w.hasWindow()) {
        QStringList peClauses;
        peClauses.append(QStringLiteral("pe.track_id = ts.track_id"));
        appendWindowConditions(w, peClauses, binds);
        peClauses.append(QStringLiteral("pe.completed = 1"));
        return QStringLiteral("(SELECT COUNT(*) FROM play_events pe WHERE %1)")
            .arg(peClauses.join(QStringLiteral(" AND ")));
    }
    return QStringLiteral(
        "(SELECT COUNT(*) FROM play_events pe WHERE pe.track_id = ts.track_id AND pe.completed = "
        "1)");
}

QString buildNumberConditionSql(const SmartCondition &cond, const SmartRule &rule,
    const PlayCountRule &countRule, QList<QVariant> &binds)
{
    QString expr;
    if (cond.field == SmartField::Year) {
        expr = QStringLiteral("COALESCE(ts.year, 0)");
    } else if (cond.field == SmartField::Rating) {
        expr = QStringLiteral(
            "COALESCE((SELECT r.rating FROM ratings r WHERE r.track_id = ts.track_id), 0)");
    } else if (cond.field == SmartField::DurationSec) {
        expr = QStringLiteral("COALESCE(ts.duration_ms, 0) / 1000");
    } else if (cond.field == SmartField::AlbumCompletion) {
        expr = QStringLiteral(
            "COALESCE((SELECT apc.completion * 100.0 FROM album_play_completion apc WHERE "
            "apc.album_id = ts.album_id), 0.0)");
    } else if (cond.field == SmartField::PlayCount) {
        expr = buildPlayCountSqlExpr(rule, countRule, binds);
    } else if (cond.field == SmartField::SkipCount) {
        expr = buildSkipCountExpr(rule, binds);
    } else if (cond.field == SmartField::CompletedCount) {
        expr = buildCompletedCountExpr(rule, binds);
    }

    const bool isFloat = (cond.field == SmartField::AlbumCompletion);
    auto bindVal = [&](const QVariant &v) {
        if (isFloat) {
            binds.append(v.toDouble());
        } else {
            binds.append(v.toLongLong());
        }
    };

    switch (cond.op) {
    case SmartOp::Equals:
        bindVal(cond.value);
        return QStringLiteral("%1 = ?").arg(expr);
    case SmartOp::NotEquals:
        bindVal(cond.value);
        return QStringLiteral("%1 != ?").arg(expr);
    case SmartOp::Greater:
        bindVal(cond.value);
        return QStringLiteral("%1 > ?").arg(expr);
    case SmartOp::Less:
        bindVal(cond.value);
        return QStringLiteral("%1 < ?").arg(expr);
    case SmartOp::Between:
        bindVal(cond.value);
        bindVal(cond.value2);
        return QStringLiteral("%1 BETWEEN ? AND ?").arg(expr);
    default:
        return { };
    }
}

QString buildBoolConditionSql(const SmartCondition &cond)
{
    if (cond.field == SmartField::Favorite) {
        if (cond.op == SmartOp::IsTrue) {
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'track' AND "
                "fav.entity_id = ts.track_id)");
        }
        if (cond.op == SmartOp::IsFalse) {
            return QStringLiteral(
                "NOT EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'track' AND "
                "fav.entity_id = ts.track_id)");
        }
    } else if (cond.field == SmartField::AlbumFavorite) {
        if (cond.op == SmartOp::IsTrue) {
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'album' AND "
                "fav.entity_id = ts.album_id)");
        }
        if (cond.op == SmartOp::IsFalse) {
            return QStringLiteral(
                "NOT EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'album' AND "
                "fav.entity_id = ts.album_id)");
        }
    } else if (cond.field == SmartField::ArtistFavorite) {
        if (cond.op == SmartOp::IsTrue) {
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM track_artists ta JOIN favorites fav ON fav.entity_type = "
                "'artist' AND fav.entity_id = ta.artist_id WHERE ta.track_id = ts.track_id AND "
                "ta.role = 'artist')");
        }
        if (cond.op == SmartOp::IsFalse) {
            return QStringLiteral(
                "NOT EXISTS (SELECT 1 FROM track_artists ta JOIN favorites fav ON fav.entity_type "
                "= "
                "'artist' AND fav.entity_id = ta.artist_id WHERE ta.track_id = ts.track_id AND "
                "ta.role = 'artist')");
        }
    }
    return { };
}

QString buildDateConditionSql(const SmartCondition &cond, QList<QVariant> &binds)
{
    if (cond.field == SmartField::DateAdded) {
        const QString expr = QStringLiteral("COALESCE(ts.added_at, 0)");
        if (cond.op == SmartOp::InLastDays) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 cutoffMs = nowMs - (cond.value.toLongLong() * 86400000LL);
            binds.append(cutoffMs);
            return QStringLiteral("%1 >= ?").arg(expr);
        }
        if (cond.op == SmartOp::NotInLastDays) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 cutoffMs = nowMs - (cond.value.toLongLong() * 86400000LL);
            binds.append(cutoffMs);
            return QStringLiteral("(%1 < ? OR %1 IS NULL)").arg(expr);
        }
        if (cond.op == SmartOp::Between) {
            const QDate d1 = QDate::fromString(cond.value.toString(), Qt::ISODate);
            const QDate d2 = QDate::fromString(cond.value2.toString(), Qt::ISODate);
            const qint64 fromMs = d1.startOfDay().toMSecsSinceEpoch();
            const qint64 toMs = d2.addDays(1).startOfDay().toMSecsSinceEpoch();
            binds.append(fromMs);
            binds.append(toMs);
            return QStringLiteral("(%1 >= ? AND %1 < ?)").arg(expr);
        }
    } else if (cond.field == SmartField::LastPlayed) {
        if (cond.op == SmartOp::InLastDays) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 cutoffMs = nowMs - (cond.value.toLongLong() * 86400000LL);
            binds.append(cutoffMs);
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM track_play_stats tps WHERE tps.track_id = ts.track_id AND "
                "tps.last_played_at >= ?)");
        }
        if (cond.op == SmartOp::NotInLastDays) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 cutoffMs = nowMs - (cond.value.toLongLong() * 86400000LL);
            binds.append(cutoffMs);
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM track_play_stats tps WHERE tps.track_id = ts.track_id AND "
                "tps.last_played_at IS NOT NULL AND tps.last_played_at < ?)");
        }
        if (cond.op == SmartOp::Between) {
            const QDate d1 = QDate::fromString(cond.value.toString(), Qt::ISODate);
            const QDate d2 = QDate::fromString(cond.value2.toString(), Qt::ISODate);
            const qint64 fromMs = d1.startOfDay().toMSecsSinceEpoch();
            const qint64 toMs = d2.addDays(1).startOfDay().toMSecsSinceEpoch();
            binds.append(fromMs);
            binds.append(toMs);
            return QStringLiteral(
                "EXISTS (SELECT 1 FROM track_play_stats tps WHERE tps.track_id = ts.track_id AND "
                "tps.last_played_at >= ? AND tps.last_played_at < ?)");
        }
    }
    return { };
}

QString buildEnumConditionSql(const SmartCondition &cond, QList<QVariant> &binds)
{
    if (cond.field == SmartField::VersionType) {
        binds.append(cond.value.toString());
        if (cond.op == SmartOp::Is) {
            return QStringLiteral(
                "COALESCE((SELECT tv.version_type FROM track_versions tv WHERE tv.track_id = "
                "ts.track_id), 'studio') = ?");
        }
        if (cond.op == SmartOp::IsNot) {
            return QStringLiteral(
                "COALESCE((SELECT tv.version_type FROM track_versions tv WHERE tv.track_id = "
                "ts.track_id), 'studio') != ?");
        }
    } else if (cond.field == SmartField::Language) {
        binds.append(cond.value.toString());
        if (cond.op == SmartOp::Is) {
            return QStringLiteral("linernotes_lang(ts.title, ts.album, ts.artist) = ?");
        }
        if (cond.op == SmartOp::IsNot) {
            return QStringLiteral("linernotes_lang(ts.title, ts.album, ts.artist) != ?");
        }
    }
    return { };
}

QString buildSingleConditionSql(const SmartCondition &cond, const SmartRule &rule,
    const PlayCountRule &countRule, QList<QVariant> &binds)
{
    switch (smartFieldKind(cond.field)) {
    case SmartFieldKind::Text:
        return buildTextConditionSql(cond, binds);
    case SmartFieldKind::Number:
        return buildNumberConditionSql(cond, rule, countRule, binds);
    case SmartFieldKind::Bool:
        return buildBoolConditionSql(cond);
    case SmartFieldKind::Date:
        return buildDateConditionSql(cond, binds);
    case SmartFieldKind::Enum:
        return buildEnumConditionSql(cond, binds);
    }
    return { };
}

} // namespace

QString buildSmartRuleWhereSql(
    const SmartRule &rule, QList<QVariant> &binds, const PlayCountRule &countRule)
{
    if (rule.conditions.isEmpty()) {
        return { };
    }

    QStringList condSqls;
    condSqls.reserve(rule.conditions.size());

    for (const auto &cond : rule.conditions) {
        const QString sql = buildSingleConditionSql(cond, rule, countRule, binds);
        if (!sql.isEmpty()) {
            condSqls.append(sql);
        }
    }

    if (condSqls.isEmpty()) {
        return { };
    }

    if (condSqls.size() == 1) {
        return QStringLiteral(" AND (%1)").arg(condSqls.first());
    }

    const QString joiner
        = (rule.match == SmartMatch::All) ? QStringLiteral(" AND ") : QStringLiteral(" OR ");
    return QStringLiteral(" AND (%1)").arg(condSqls.join(joiner));
}

} // namespace linernotes::library::detail
