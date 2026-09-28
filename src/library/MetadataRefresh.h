// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>

#include <core/Result.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <utility>

namespace linernotes::library::detail {

core::Result<void> execWrite(QSqlQuery &q, const QString &detail = QString());

template <typename F>
auto inTransaction(Database &db, F &&body) -> decltype(body(std::declval<const QSqlDatabase &>()))
{
    auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();
    Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction"),
            .detail = QString(),
        };
    }

    auto res = std::forward<F>(body)(conn);
    if (!res.ok()) {
        return res;
    }

    if (auto commitRes = tx.commit(); !commitRes.ok()) {
        return commitRes.error();
    }

    return res;
}

core::Result<void> relinkTracks(const QSqlDatabase &conn, const QList<qint64> &trackIds);

} // namespace linernotes::library::detail
