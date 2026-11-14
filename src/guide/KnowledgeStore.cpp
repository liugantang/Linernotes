// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "KnowledgeStore.h"

#include "Errors.h"
#include "Knowledge.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace linernotes::guide {

namespace {

constexpr qint64 kFoundTtlMs = 180LL * 24 * 60 * 60 * 1000;
constexpr qint64 kNotFoundTtlMs = 30LL * 24 * 60 * 60 * 1000;

} // namespace

KnowledgeStore::KnowledgeStore(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

core::Result<CachedLookup> KnowledgeStore::get(const QString &key) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT qid, fetched_at FROM knowledge_lookups WHERE key = ?;"));
    q.addBindValue(key);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to query knowledge lookup"),
            .detail = q.lastError().text(),
        };
    }
    if (!q.next()) {
        return CachedLookup { .state = LookupState::Missing };
    }

    const QVariant qidVal = q.value(0);
    const qint64 lookupFetchedAt = q.value(1).toLongLong();
    const qint64 now = m_clock.nowMs();

    if (qidVal.isNull()) {
        if (now - lookupFetchedAt < kNotFoundTtlMs) {
            return CachedLookup { .state = LookupState::NotFound };
        }
        return CachedLookup { .state = LookupState::Missing };
    }

    if (now - lookupFetchedAt >= kFoundTtlMs) {
        return CachedLookup { .state = LookupState::Missing };
    }

    const QString qid = qidVal.toString();
    QSqlQuery entryQ(conn);
    entryQ.prepare(
        QStringLiteral("SELECT payload, fetched_at FROM knowledge_entries WHERE qid = ?;"));
    entryQ.addBindValue(qid);
    if (!entryQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to query knowledge entry"),
            .detail = entryQ.lastError().text(),
        };
    }
    if (!entryQ.next()) {
        return CachedLookup { .state = LookupState::Missing };
    }

    const QString payload = entryQ.value(0).toString();
    const qint64 entryFetchedAt = entryQ.value(1).toLongLong();
    if (now - entryFetchedAt >= kFoundTtlMs) {
        return CachedLookup { .state = LookupState::Missing };
    }

    const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
    if (!doc.isObject()) {
        return CachedLookup { .state = LookupState::Missing };
    }
    const auto entry = knowledgeEntryFromJson(doc.object());
    if (!entry.has_value()) {
        return CachedLookup { .state = LookupState::Missing };
    }

    return CachedLookup { .state = LookupState::Found, .entry = entry.value() };
}

core::Result<void> KnowledgeStore::saveFound(const QString &key, const KnowledgeEntry &entry) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to start transaction"),
            .detail = QString(),
        };
    }

    QString kindStr;
    switch (entry.kind) {
    case KnowledgeKind::Artist:
        kindStr = QStringLiteral("artist");
        break;
    case KnowledgeKind::Song:
        kindStr = QStringLiteral("song");
        break;
    case KnowledgeKind::Album:
        kindStr = QStringLiteral("album");
        break;
    }

    const QJsonObject obj = knowledgeEntryToJson(entry);
    const QString payload = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    const qint64 now = m_clock.nowMs();

    QSqlQuery entryQ(conn);
    entryQ.prepare(QStringLiteral(
        "INSERT INTO knowledge_entries (qid, kind, payload, fetched_at) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(qid) DO UPDATE SET "
        "kind = excluded.kind, payload = excluded.payload, fetched_at = excluded.fetched_at;"));
    entryQ.addBindValue(entry.qid);
    entryQ.addBindValue(kindStr);
    entryQ.addBindValue(payload);
    entryQ.addBindValue(now);
    if (!entryQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to insert knowledge entry"),
            .detail = entryQ.lastError().text(),
        };
    }

    QSqlQuery lookupQ(conn);
    lookupQ.prepare(QStringLiteral("INSERT INTO knowledge_lookups (key, qid, fetched_at) "
                                   "VALUES (?, ?, ?) "
                                   "ON CONFLICT(key) DO UPDATE SET "
                                   "qid = excluded.qid, fetched_at = excluded.fetched_at;"));
    lookupQ.addBindValue(key);
    lookupQ.addBindValue(entry.qid);
    lookupQ.addBindValue(now);
    if (!lookupQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to insert knowledge lookup"),
            .detail = lookupQ.lastError().text(),
        };
    }

    return tx.commit();
}

core::Result<void> KnowledgeStore::saveNotFound(const QString &key) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    const qint64 now = m_clock.nowMs();
    QSqlQuery lookupQ(conn);
    lookupQ.prepare(QStringLiteral("INSERT INTO knowledge_lookups (key, qid, fetched_at) "
                                   "VALUES (?, NULL, ?) "
                                   "ON CONFLICT(key) DO UPDATE SET "
                                   "qid = NULL, fetched_at = excluded.fetched_at;"));
    lookupQ.addBindValue(key);
    lookupQ.addBindValue(now);
    if (!lookupQ.exec()) {
        return core::Error {
            .code = QString(errc::kDbFailed),
            .message = QStringLiteral("Failed to save not found knowledge lookup"),
            .detail = lookupQ.lastError().text(),
        };
    }

    return { };
}

} // namespace linernotes::guide
