// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SmartRuleSql.h"

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

QString buildSingleConditionSql(const SmartCondition &cond, QList<QVariant> &binds)
{
    switch (cond.field) {
    case SmartField::Title:
    case SmartField::Artist:
    case SmartField::Album:
    case SmartField::AlbumArtist:
    case SmartField::Genre:
    case SmartField::Codec: {
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

    case SmartField::Year:
    case SmartField::Rating:
    case SmartField::DurationSec: {
        QString expr;
        if (cond.field == SmartField::Year) {
            expr = QStringLiteral("COALESCE(ts.year, 0)");
        } else if (cond.field == SmartField::Rating) {
            expr = QStringLiteral(
                "COALESCE((SELECT r.rating FROM ratings r WHERE r.track_id = ts.track_id), 0)");
        } else if (cond.field == SmartField::DurationSec) {
            expr = QStringLiteral("COALESCE(ts.duration_ms, 0) / 1000");
        }

        switch (cond.op) {
        case SmartOp::Equals:
            binds.append(cond.value.toLongLong());
            return QStringLiteral("%1 = ?").arg(expr);
        case SmartOp::NotEquals:
            binds.append(cond.value.toLongLong());
            return QStringLiteral("%1 != ?").arg(expr);
        case SmartOp::Greater:
            binds.append(cond.value.toLongLong());
            return QStringLiteral("%1 > ?").arg(expr);
        case SmartOp::Less:
            binds.append(cond.value.toLongLong());
            return QStringLiteral("%1 < ?").arg(expr);
        case SmartOp::Between:
            binds.append(cond.value.toLongLong());
            binds.append(cond.value2.toLongLong());
            return QStringLiteral("%1 BETWEEN ? AND ?").arg(expr);
        default:
            return { };
        }
    }

    case SmartField::Favorite: {
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
        return { };
    }

    case SmartField::DateAdded: {
        const QString expr = QStringLiteral("COALESCE(ts.added_at, 0)");
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 cutoffMs = nowMs - (cond.value.toLongLong() * 86400000LL);

        if (cond.op == SmartOp::InLastDays) {
            binds.append(cutoffMs);
            return QStringLiteral("%1 >= ?").arg(expr);
        }
        if (cond.op == SmartOp::NotInLastDays) {
            binds.append(cutoffMs);
            return QStringLiteral("(%1 < ? OR %1 IS NULL)").arg(expr);
        }
        return { };
    }
    }

    return { };
}

} // namespace

QString buildSmartRuleWhereSql(const SmartRule &rule, QList<QVariant> &binds)
{
    if (rule.conditions.isEmpty()) {
        return { };
    }

    QStringList condSqls;
    condSqls.reserve(rule.conditions.size());

    for (const auto &cond : rule.conditions) {
        const QString sql = buildSingleConditionSql(cond, binds);
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
