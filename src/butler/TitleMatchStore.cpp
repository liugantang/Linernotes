// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <butler/TitleMatchStore.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

TitleMatchStore::TitleMatchStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<QHash<QPair<QString, QString>, TitleMatchVerdict>> TitleMatchStore::loadAll(
    int promptVersion) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT key_a, key_b, same, confidence, reason "
                             "FROM title_matches WHERE prompt_version = ?"));
    q.addBindValue(promptVersion);

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString(),
        };
    }

    QHash<QPair<QString, QString>, TitleMatchVerdict> results;
    while (q.next()) {
        const QString keyA = q.value(0).toString();
        const QString keyB = q.value(1).toString();
        const bool same = (q.value(2).toInt() != 0);
        const double confidence = q.value(3).toDouble();
        const QString reason = q.value(4).toString();

        results.insert(qMakePair(keyA, keyB),
            TitleMatchVerdict {
                .same = same,
                .confidence = confidence,
                .reason = reason,
            });
    }

    return results;
}

core::Result<void> TitleMatchStore::save(
    const QHash<QPair<QString, QString>, TitleMatchVerdict> &verdicts, const QString &model,
    int promptVersion) const
{
    if (verdicts.isEmpty()) {
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
            .message = QStringLiteral("Failed to begin transaction for title match save"),
            .detail = QString(),
        };
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO title_matches "
        "(key_a, key_b, same, confidence, reason, model, prompt_version, decided_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

    for (auto it = verdicts.cbegin(); it != verdicts.cend(); ++it) {
        QString keyA = it.key().first;
        QString keyB = it.key().second;
        if (keyA > keyB) {
            std::swap(keyA, keyB);
        }

        const TitleMatchVerdict &verdict = it.value();

        q.bindValue(0, keyA);
        q.bindValue(1, keyB);
        q.bindValue(2, verdict.same ? 1 : 0);
        q.bindValue(3, verdict.confidence);
        q.bindValue(4, verdict.reason);
        q.bindValue(5, model);
        q.bindValue(6, promptVersion);
        q.bindValue(7, now);

        if (!q.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = q.lastError().text(),
                .detail = QStringLiteral("%1 <-> %2").arg(keyA, keyB),
            };
        }
    }

    return tx.commit();
}

} // namespace linernotes::butler
