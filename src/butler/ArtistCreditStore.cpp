// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCreditStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

namespace linernotes::butler {

ArtistCreditStore::ArtistCreditStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<std::optional<StoredArtistCredit>> ArtistCreditStore::load(const QString &value) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT result, model, prompt_version, parsed_at FROM artist_credits WHERE value = ?"));
    q.addBindValue(value);

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = value,
        };
    }

    if (!q.next()) {
        return std::optional<StoredArtistCredit>(std::nullopt);
    }

    const QString jsonStr = q.value(0).toString();
    const QString model = q.value(1).toString();
    const int promptVersion = q.value(2).toInt();
    const qint64 parsedAt = q.value(3).toLongLong();

    QJsonParseError parseErr { };
    const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
    if (doc.isNull() || !doc.isObject()) {
        return std::optional<StoredArtistCredit>(std::nullopt);
    }

    const auto creditOpt = artistCreditFromJson(doc.object());
    if (!creditOpt.has_value()) {
        return std::optional<StoredArtistCredit>(std::nullopt);
    }

    return std::optional<StoredArtistCredit>(StoredArtistCredit {
        .credit = *creditOpt,
        .model = model,
        .promptVersion = promptVersion,
        .parsedAt = parsedAt,
    });
}

core::Result<QHash<QString, StoredArtistCredit>> ArtistCreditStore::loadAll() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    if (!q.exec(QStringLiteral(
            "SELECT value, result, model, prompt_version, parsed_at FROM artist_credits"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QHash<QString, StoredArtistCredit> result;
    while (q.next()) {
        const QString val = q.value(0).toString();
        const QString jsonStr = q.value(1).toString();
        const QString model = q.value(2).toString();
        const int promptVersion = q.value(3).toInt();
        const qint64 parsedAt = q.value(4).toLongLong();

        QJsonParseError parseErr { };
        const auto doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseErr);
        if (doc.isNull() || !doc.isObject()) {
            continue;
        }

        const auto creditOpt = artistCreditFromJson(doc.object());
        if (!creditOpt.has_value()) {
            continue;
        }

        result.insert(val,
            StoredArtistCredit {
                .credit = *creditOpt,
                .model = model,
                .promptVersion = promptVersion,
                .parsedAt = parsedAt,
            });
    }

    return result;
}

core::Result<void> ArtistCreditStore::save(
    const QHash<QString, ArtistCredit> &credits, const QString &model, int promptVersion)
{
    if (credits.isEmpty()) {
        return { };
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const qint64 now = m_clock.nowMs();

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for artist credit save"),
            .detail = QString(),
        };
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO artist_credits "
                             "(value, result, model, prompt_version, parsed_at) "
                             "VALUES (?, ?, ?, ?, ?)"));

    for (auto it = credits.cbegin(); it != credits.cend(); ++it) {
        const QString &val = it.key();
        const ArtistCredit &credit = it.value();
        const QJsonObject obj = toJson(credit);
        const QString jsonStr
            = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));

        q.bindValue(0, val);
        q.bindValue(1, jsonStr);
        q.bindValue(2, model);
        q.bindValue(3, promptVersion);
        q.bindValue(4, now);

        if (!q.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = q.lastError().text(),
                .detail = val,
            };
        }
    }

    return tx.commit();
}

} // namespace linernotes::butler
