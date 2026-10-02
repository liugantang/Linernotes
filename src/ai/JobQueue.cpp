// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "JobQueue.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

#include <ai/AiEnumNames.h>
#include <ai/AiLogging.h>
#include <ai/Errors.h>
#include <library/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::ai {

namespace {

constexpr int kDbErrorRetryDelayMs = 2000;

bool isConfigError(const QString &code)
{
    return code == errc::kAuth || code == errc::kNotConfigured || code == errc::kPrivacyBlocked
        || code == errc::kSecretStore;
}

JobInfo jobInfoFromQuery(const QSqlQuery &query)
{
    JobInfo info;
    info.id = query.value(0).toLongLong();
    info.kind = query.value(1).toString();
    info.title = query.value(2).toString();
    info.state = jobStateFromName(query.value(3).toString()).value_or(JobState::Paused);
    info.lastError = query.value(4).toString();
    info.createdAt = query.value(5).toLongLong();
    info.total = query.value(6).toInt();
    info.done = query.value(7).toInt();
    info.failed = query.value(8).toInt();
    return info;
}

constexpr const char *kJobsQuerySql = R"(
    SELECT j.id, j.kind, j.title, j.state, j.last_error, j.created_at,
           COUNT(i.seq) AS total_count,
           SUM(CASE WHEN i.state = 'done' THEN 1 ELSE 0 END) AS done_count,
           SUM(CASE WHEN i.state = 'failed' THEN 1 ELSE 0 END) AS failed_count
    FROM jobs j
    LEFT JOIN job_items i ON j.id = i.job_id
)";

} // namespace

JobQueue::JobQueue(library::Database &db, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_clock(clock)
{
}

JobQueue::~JobQueue()
{
    m_inFlight.clear();
    m_handlers.clear();
}

void JobQueue::registerHandler(std::unique_ptr<JobHandler> handler)
{
    if (handler == nullptr) {
        return;
    }
    const QString k = handler->kind();
    if (k.isEmpty()) {
        return;
    }
    m_handlers.insert_or_assign(k, std::move(handler));
}

core::Result<TokenUsage> JobQueue::estimate(
    const QString &kind, const QStringList &items, const QJsonObject &params) const
{
    const auto it = m_handlers.find(kind);
    if (it == m_handlers.end()) {
        return core::Error {
            .code = QString(errc::kNotConfigured),
            .message = QStringLiteral("Handler not registered for kind: %1").arg(kind),
            .detail = { },
        };
    }

    TokenUsage total;
    for (const auto &item : items) {
        const TokenUsage u = it->second->estimate(item, params);
        total.promptTokens += u.promptTokens;
        total.completionTokens += u.completionTokens;
    }
    return total;
}

core::Result<qint64> JobQueue::enqueue(
    const QString &kind, const QString &title, const QStringList &items, const QJsonObject &params)
{
    const auto it = m_handlers.find(kind);
    if (it == m_handlers.end()) {
        return core::Error {
            .code = QString(errc::kNotConfigured),
            .message = QStringLiteral("Handler not registered for kind: %1").arg(kind),
            .detail = { },
        };
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn, library::Transaction::Mode::Immediate);

    const qint64 nowMs = m_clock.nowMs();
    const bool empty = items.isEmpty();
    const QString stateStr
        = empty ? jobStateName(JobState::Completed) : jobStateName(JobState::Running);
    const QString paramsJson
        = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));

    QSqlQuery insertJobQuery(conn);
    insertJobQuery.prepare(QStringLiteral(
        "INSERT INTO jobs (kind, title, params, state, last_error, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, NULL, ?, ?)"));
    insertJobQuery.addBindValue(kind);
    insertJobQuery.addBindValue(title);
    insertJobQuery.addBindValue(paramsJson);
    insertJobQuery.addBindValue(stateStr);
    insertJobQuery.addBindValue(nowMs);
    insertJobQuery.addBindValue(nowMs);

    if (!insertJobQuery.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = insertJobQuery.lastError().text(),
            .detail = insertJobQuery.lastQuery(),
        };
    }

    const qint64 jobId = insertJobQuery.lastInsertId().toLongLong();
    insertJobQuery.finish();

    if (!empty) {
        QSqlQuery insertItemQuery(conn);
        insertItemQuery.prepare(
            QStringLiteral("INSERT INTO job_items (job_id, seq, item_key, state, error_code) "
                           "VALUES (?, ?, ?, 'pending', NULL)"));

        for (int i = 0; i < items.size(); ++i) {
            insertItemQuery.addBindValue(jobId);
            insertItemQuery.addBindValue(i);
            insertItemQuery.addBindValue(items.at(i));
            if (!insertItemQuery.exec()) {
                return core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = insertItemQuery.lastError().text(),
                    .detail = insertItemQuery.lastQuery(),
                };
            }
        }
        insertItemQuery.finish();
    }

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        return commitRes.error();
    }

    emit jobChanged(jobId);

    if (!empty) {
        dispatchJob(jobId);
    }

    return jobId;
}

core::Result<void> JobQueue::restore()
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    const qint64 nowMs = m_clock.nowMs();
    QSqlQuery query(connRes.value());
    query.prepare(
        QStringLiteral("UPDATE jobs SET state = 'paused', updated_at = ? WHERE state = 'running'"));
    query.addBindValue(nowMs);

    if (!query.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = query.lastError().text(),
            .detail = query.lastQuery(),
        };
    }
    query.finish();

    return { };
}

void JobQueue::pause(qint64 jobId)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        handleDbError(jobId, connRes.error());
        return;
    }

    const qint64 nowMs = m_clock.nowMs();
    QSqlQuery query(connRes.value());
    query.prepare(QStringLiteral(
        "UPDATE jobs SET state = 'paused', updated_at = ? WHERE id = ? AND state = 'running'"));
    query.addBindValue(nowMs);
    query.addBindValue(jobId);

    if (!query.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = query.lastError().text(),
                .detail = query.lastQuery(),
            });
        return;
    }

    const int affected = query.numRowsAffected();
    query.finish();

    if (affected > 0) {
        emit jobChanged(jobId);
    }
}

void JobQueue::resume(qint64 jobId)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        handleDbError(jobId, connRes.error());
        return;
    }

    QSqlQuery jobQuery(connRes.value());
    jobQuery.prepare(QStringLiteral("SELECT kind, state FROM jobs WHERE id = ?"));
    jobQuery.addBindValue(jobId);
    if (!jobQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = jobQuery.lastError().text(),
                .detail = jobQuery.lastQuery(),
            });
        return;
    }
    if (!jobQuery.next()) {
        jobQuery.finish();
        return;
    }

    const QString kind = jobQuery.value(0).toString();
    const QString stateStr = jobQuery.value(1).toString();
    jobQuery.finish();

    if (stateStr == QStringLiteral("running")) {
        return;
    }

    if (!m_handlers.contains(kind)) {
        const QString errMsg = QString(errc::kNotConfigured)
            + QStringLiteral(": Handler not registered for ") + kind;
        updateJobState(jobId, JobState::Paused, errMsg);
        emit jobChanged(jobId);
        return;
    }

    QSqlQuery countQuery(connRes.value());
    countQuery.prepare(
        QStringLiteral("SELECT COUNT(*) FROM job_items WHERE job_id = ? AND state = 'pending'"));
    countQuery.addBindValue(jobId);
    if (!countQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = countQuery.lastError().text(),
                .detail = countQuery.lastQuery(),
            });
        return;
    }
    if (!countQuery.next()) {
        countQuery.finish();
        return;
    }

    const int pendingCount = countQuery.value(0).toInt();
    countQuery.finish();
    const bool hasInFlight = m_inFlight.contains(jobId) && !m_inFlight.at(jobId).empty();

    if (pendingCount == 0 && !hasInFlight) {
        updateJobState(jobId, JobState::Completed);
        emit jobChanged(jobId);
        return;
    }

    updateJobState(jobId, JobState::Running);
    emit jobChanged(jobId);
    dispatchJob(jobId);
}

void JobQueue::cancel(qint64 jobId)
{
    const auto it = m_inFlight.find(jobId);
    if (it != m_inFlight.end()) {
        auto works = std::move(it->second);
        m_inFlight.erase(it);
        works.clear();
    }

    updateJobState(jobId, JobState::Cancelled);
    emit jobChanged(jobId);
}

void JobQueue::retryFailed(qint64 jobId)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        handleDbError(jobId, connRes.error());
        return;
    }

    library::Transaction tx(connRes.value(), library::Transaction::Mode::Immediate);

    QSqlQuery resetItemsQuery(connRes.value());
    resetItemsQuery.prepare(QStringLiteral("UPDATE job_items SET state = 'pending', error_code = "
                                           "NULL WHERE job_id = ? AND state = 'failed'"));
    resetItemsQuery.addBindValue(jobId);
    if (!resetItemsQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = resetItemsQuery.lastError().text(),
                .detail = resetItemsQuery.lastQuery(),
            });
        return;
    }
    resetItemsQuery.finish();

    const qint64 nowMs = m_clock.nowMs();
    QSqlQuery updateJobQuery(connRes.value());
    updateJobQuery.prepare(QStringLiteral("UPDATE jobs SET state = 'running', last_error = NULL, "
                                          "updated_at = ? WHERE id = ? AND state = 'completed'"));
    updateJobQuery.addBindValue(nowMs);
    updateJobQuery.addBindValue(jobId);
    if (!updateJobQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = updateJobQuery.lastError().text(),
                .detail = updateJobQuery.lastQuery(),
            });
        return;
    }
    updateJobQuery.finish();

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        handleDbError(jobId, commitRes.error());
        return;
    }

    emit jobChanged(jobId);

    QSqlQuery checkQuery(connRes.value());
    checkQuery.prepare(QStringLiteral("SELECT state FROM jobs WHERE id = ?"));
    checkQuery.addBindValue(jobId);
    if (checkQuery.exec() && checkQuery.next()) {
        const QString state = checkQuery.value(0).toString();
        checkQuery.finish();
        if (state == QStringLiteral("running")) {
            dispatchJob(jobId);
        }
    } else {
        checkQuery.finish();
    }
}

void JobQueue::remove(qint64 jobId)
{
    cancel(jobId);

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        handleDbError(jobId, connRes.error());
        return;
    }

    library::Transaction tx(connRes.value(), library::Transaction::Mode::Immediate);

    QSqlQuery deleteItemsQuery(connRes.value());
    deleteItemsQuery.prepare(QStringLiteral("DELETE FROM job_items WHERE job_id = ?"));
    deleteItemsQuery.addBindValue(jobId);
    if (!deleteItemsQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = deleteItemsQuery.lastError().text(),
                .detail = deleteItemsQuery.lastQuery(),
            });
        return;
    }
    deleteItemsQuery.finish();

    QSqlQuery deleteJobQuery(connRes.value());
    deleteJobQuery.prepare(QStringLiteral("DELETE FROM jobs WHERE id = ?"));
    deleteJobQuery.addBindValue(jobId);
    if (!deleteJobQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = deleteJobQuery.lastError().text(),
                .detail = deleteJobQuery.lastQuery(),
            });
        return;
    }
    deleteJobQuery.finish();

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        handleDbError(jobId, commitRes.error());
        return;
    }

    emit jobChanged(jobId);
}

QList<JobInfo> JobQueue::jobs() const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        qCWarning(lcAi) << "JobQueue::jobs failed to get db connection:" << connRes.error().message;
        return { };
    }

    const QString sql = QString::fromUtf8(kJobsQuerySql)
        + QStringLiteral(" GROUP BY j.id ORDER BY j.created_at DESC, j.id DESC");

    QSqlQuery query(connRes.value());
    if (!query.exec(sql)) {
        qCWarning(lcAi) << "JobQueue::jobs query failed:" << query.lastError().text();
        return { };
    }

    QList<JobInfo> results;
    while (query.next()) {
        results.append(jobInfoFromQuery(query));
    }
    query.finish();
    return results;
}

std::optional<JobInfo> JobQueue::job(qint64 jobId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        qCWarning(lcAi) << "JobQueue::job failed to get db connection:" << connRes.error().message;
        return std::nullopt;
    }

    const QString sql
        = QString::fromUtf8(kJobsQuerySql) + QStringLiteral(" WHERE j.id = ? GROUP BY j.id");

    QSqlQuery query(connRes.value());
    query.prepare(sql);
    query.addBindValue(jobId);
    if (!query.exec()) {
        qCWarning(lcAi) << "JobQueue::job query failed:" << query.lastError().text();
        return std::nullopt;
    }

    if (!query.next()) {
        query.finish();
        return std::nullopt;
    }

    auto info = jobInfoFromQuery(query);
    query.finish();
    return info;
}

void JobQueue::dispatchJob(qint64 jobId)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        handleDbError(jobId, connRes.error());
        return;
    }

    QSqlQuery jobQuery(connRes.value());
    jobQuery.prepare(QStringLiteral("SELECT kind, params, state FROM jobs WHERE id = ?"));
    jobQuery.addBindValue(jobId);
    if (!jobQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = jobQuery.lastError().text(),
                .detail = jobQuery.lastQuery(),
            });
        return;
    }
    if (!jobQuery.next()) {
        jobQuery.finish();
        return;
    }

    const QString kind = jobQuery.value(0).toString();
    const QString paramsStr = jobQuery.value(1).toString();
    const QString stateStr = jobQuery.value(2).toString();
    jobQuery.finish();

    if (stateStr != QStringLiteral("running")) {
        return;
    }

    const auto it = m_handlers.find(kind);
    if (it == m_handlers.end()) {
        const QString errMsg = QString(errc::kNotConfigured)
            + QStringLiteral(": Handler not registered for ") + kind;
        updateJobState(jobId, JobState::Paused, errMsg);
        emit jobChanged(jobId);
        return;
    }

    JobHandler *handler = it->second.get();
    const int maxInFlight = std::max(1, handler->maxInFlight());

    auto &inFlightMap = m_inFlight[jobId];
    const int currentInFlight = static_cast<int>(inFlightMap.size());
    const int availableSlots = maxInFlight - currentInFlight;

    if (availableSlots <= 0) {
        return;
    }

    QSqlQuery itemsQuery(connRes.value());
    itemsQuery.prepare(QStringLiteral(
        "SELECT seq, item_key FROM job_items WHERE job_id = ? AND state = 'pending' ORDER BY seq "
        "ASC"));
    itemsQuery.addBindValue(jobId);
    if (!itemsQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = itemsQuery.lastError().text(),
                .detail = itemsQuery.lastQuery(),
            });
        return;
    }

    QList<PendingItem> pendingItems;
    bool hasAnyPending = false;

    while (itemsQuery.next()) {
        hasAnyPending = true;
        const int seq = itemsQuery.value(0).toInt();
        if (!inFlightMap.contains(seq)) {
            pendingItems.append(PendingItem {
                .seq = seq,
                .itemKey = itemsQuery.value(1).toString(),
            });
            if (pendingItems.size() >= availableSlots) {
                break;
            }
        }
    }
    itemsQuery.finish();

    if (pendingItems.isEmpty()) {
        if (inFlightMap.empty() && !hasAnyPending) {
            updateJobState(jobId, JobState::Completed);
            emit jobChanged(jobId);
        }
        return;
    }

    QJsonObject paramsObj;
    if (!paramsStr.isEmpty()) {
        paramsObj = QJsonDocument::fromJson(paramsStr.toUtf8()).object();
    }

    dispatchPendingItems(jobId, handler, paramsObj, pendingItems);
}

void JobQueue::dispatchPendingItems(qint64 jobId, JobHandler *handler, const QJsonObject &params,
    const QList<PendingItem> &pendingItems)
{
    auto &inFlightMap = m_inFlight[jobId];

    for (const auto &item : pendingItems) {
        const int seq = item.seq;
        const QString itemKey = item.itemKey;

        auto done = [guard = QPointer<JobQueue>(this), jobId, seq](const core::Result<void> &res) {
            if (guard.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, jobId, seq, res] {
                    if (!guard.isNull()) {
                        guard->handleItemDone(jobId, seq, res);
                    }
                },
                Qt::QueuedConnection);
        };

        std::unique_ptr<QObject> work = handler->process(itemKey, params, done);
        inFlightMap.insert_or_assign(seq, std::move(work));
    }
}

void JobQueue::handleItemDone(qint64 jobId, int seq, const core::Result<void> &result)
{
    const auto jobIt = m_inFlight.find(jobId);
    if (jobIt == m_inFlight.end()) {
        return;
    }

    const auto itemIt = jobIt->second.find(seq);
    if (itemIt == jobIt->second.end()) {
        return;
    }

    auto work = std::move(itemIt->second);
    jobIt->second.erase(itemIt);
    if (jobIt->second.empty()) {
        m_inFlight.erase(jobIt);
    }

    if (work != nullptr) {
        auto *raw = work.release();
        raw->deleteLater();
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        handleDbError(jobId, connRes.error());
        return;
    }

    QSqlQuery stateQuery(connRes.value());
    stateQuery.prepare(QStringLiteral("SELECT state FROM jobs WHERE id = ?"));
    stateQuery.addBindValue(jobId);
    if (!stateQuery.exec()) {
        handleDbError(jobId,
            core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = stateQuery.lastError().text(),
                .detail = stateQuery.lastQuery(),
            });
        return;
    }
    if (!stateQuery.next()) {
        stateQuery.finish();
        return;
    }

    const QString currentState = stateQuery.value(0).toString();
    stateQuery.finish();

    if (currentState == QStringLiteral("cancelled")) {
        return;
    }

    const qint64 nowMs = m_clock.nowMs();

    if (result.ok()) {
        QSqlQuery updateItem(connRes.value());
        updateItem.prepare(QStringLiteral(
            "UPDATE job_items SET state = 'done', error_code = NULL WHERE job_id = ? AND seq = ?"));
        updateItem.addBindValue(jobId);
        updateItem.addBindValue(seq);
        if (!updateItem.exec()) {
            handleDbError(jobId,
                core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = updateItem.lastError().text(),
                    .detail = updateItem.lastQuery(),
                });
            return;
        }
        updateItem.finish();

        QSqlQuery updateJob(connRes.value());
        updateJob.prepare(QStringLiteral("UPDATE jobs SET updated_at = ? WHERE id = ?"));
        updateJob.addBindValue(nowMs);
        updateJob.addBindValue(jobId);
        updateJob.exec();
        updateJob.finish();

        emit jobChanged(jobId);
    } else if (isConfigError(result.error().code)) {
        const QString lastError
            = result.error().code + QStringLiteral(": ") + result.error().message;
        updateJobState(jobId, JobState::Paused, lastError);
        emit jobChanged(jobId);
        return;
    } else {
        QSqlQuery updateItem(connRes.value());
        updateItem.prepare(QStringLiteral(
            "UPDATE job_items SET state = 'failed', error_code = ? WHERE job_id = ? AND seq = ?"));
        updateItem.addBindValue(result.error().code);
        updateItem.addBindValue(jobId);
        updateItem.addBindValue(seq);
        if (!updateItem.exec()) {
            handleDbError(jobId,
                core::Error {
                    .code = QString(library::errc::kDbQuery),
                    .message = updateItem.lastError().text(),
                    .detail = updateItem.lastQuery(),
                });
            return;
        }
        updateItem.finish();

        QSqlQuery updateJob(connRes.value());
        updateJob.prepare(QStringLiteral("UPDATE jobs SET updated_at = ? WHERE id = ?"));
        updateJob.addBindValue(nowMs);
        updateJob.addBindValue(jobId);
        updateJob.exec();
        updateJob.finish();

        emit jobChanged(jobId);
    }

    if (currentState == QStringLiteral("running")) {
        dispatchJob(jobId);
    }
}

void JobQueue::updateJobState(qint64 jobId, JobState state, const QString &lastError)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        qCWarning(lcAi) << "JobQueue::updateJobState failed to get db connection:"
                        << connRes.error().message;
        return;
    }

    const qint64 nowMs = m_clock.nowMs();
    QSqlQuery query(connRes.value());
    query.prepare(
        QStringLiteral("UPDATE jobs SET state = ?, last_error = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(jobStateName(state));
    if (lastError.isEmpty()) {
        query.addBindValue(QVariant(QMetaType::fromType<QString>()));
    } else {
        query.addBindValue(lastError);
    }
    query.addBindValue(nowMs);
    query.addBindValue(jobId);

    if (!query.exec()) {
        qCWarning(lcAi) << "JobQueue::updateJobState failed:" << query.lastError().text();
    }
    query.finish();
}

void JobQueue::handleDbError(qint64 jobId, const core::Error &error)
{
    qCWarning(lcAi) << "JobQueue database error for job" << jobId << ":" << error.message;
    if (jobId <= 0) {
        return;
    }
    QTimer::singleShot(kDbErrorRetryDelayMs, this, [guard = QPointer<JobQueue>(this), jobId]() {
        if (!guard.isNull()) {
            guard->dispatchJob(jobId);
        }
    });
}

} // namespace linernotes::ai
