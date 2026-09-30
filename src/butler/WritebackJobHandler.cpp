// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "WritebackJobHandler.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent>

#include <core/Clock.h>
#include <library/Database.h>
#include <library/Errors.h>
#include <library/TagWriter.h>
#include <library/WritebackStore.h>

#include <utility>

namespace linernotes::butler {

namespace {

constexpr int kMaxWritebackThreads = 2;

class JobActionWorker final : public QObject {
    Q_OBJECT
public:
    JobActionWorker(const QFuture<core::Result<void>> &future,
        std::function<void(const core::Result<void> &)> done)
        : m_done(std::move(done))
    {
        connect(&m_watcher, &QFutureWatcher<core::Result<void>>::finished, this,
            &JobActionWorker::onFinished);
        m_watcher.setFuture(future);
    }

    ~JobActionWorker() override
    {
        m_done = nullptr;
        m_watcher.disconnect(this);
    }

    Q_DISABLE_COPY_MOVE(JobActionWorker)

private slots:
    void onFinished()
    {
        if (!m_done) {
            return;
        }
        const auto res = m_watcher.result();
        auto done = std::move(m_done);
        m_done = nullptr;
        done(res);
    }

private:
    std::function<void(const core::Result<void> &)> m_done;
    QFutureWatcher<core::Result<void>> m_watcher;
};

} // namespace

WritebackJobHandler::WritebackJobHandler(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
    m_pool.setMaxThreadCount(kMaxWritebackThreads);
}

WritebackJobHandler::~WritebackJobHandler()
{
    m_pool.waitForDone();
}

QString WritebackJobHandler::kind() const
{
    return QStringLiteral("library.writeback");
}

int WritebackJobHandler::maxInFlight() const
{
    return m_pool.maxThreadCount();
}

ai::TokenUsage WritebackJobHandler::estimate(
    const QString & /*itemKey*/, const QJsonObject & /*params*/) const
{
    return ai::TokenUsage {
        .promptTokens = 0,
        .completionTokens = 0,
    };
}

std::unique_ptr<QObject> WritebackJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    bool ok = false;
    const qint64 fileId = itemKey.toLongLong(&ok);
    const qint64 writebackId = params.value(QStringLiteral("writebackId")).toInteger();
    if (!ok || fileId <= 0 || writebackId <= 0) {
        done(core::Error {
            .code = QString(library::errc::kWritebackInvalid),
            .message = QStringLiteral("Invalid fileId or writebackId"),
            .detail = itemKey,
        });
        return nullptr;
    }

    auto future = QtConcurrent::run(
        &m_pool, [&db = m_db, &clock = m_clock, writebackId, fileId]() -> core::Result<void> {
            library::WritebackStore store(db, clock);
            const auto recRes = store.fileRecord(writebackId, fileId);
            if (!recRes.ok()) {
                return recRes.error();
            }
            const auto &record = recRes.value();

            const auto snapRes = library::TagWriter::snapshot(record.path);
            if (!snapRes.ok()) {
                static_cast<void>(store.markFailed(writebackId, fileId,
                    library::WritebackFileStatus::Failed, snapRes.error().message));
                return snapRes.error();
            }

            const auto snapObj = snapRes.value().toJson();
            const QString snapJson
                = QString::fromUtf8(QJsonDocument(snapObj).toJson(QJsonDocument::Compact));
            const auto saveRes = store.saveSnapshot(writebackId, fileId, snapJson);
            if (!saveRes.ok()) {
                static_cast<void>(store.markFailed(writebackId, fileId,
                    library::WritebackFileStatus::Failed, saveRes.error().message));
                return saveRes.error();
            }

            const auto writeRes = library::TagWriter::writeFields(record.path, record.fields);
            if (!writeRes.ok()) {
                static_cast<void>(library::TagWriter::restore(record.path, snapRes.value()));
                static_cast<void>(store.markFailed(writebackId, fileId,
                    library::WritebackFileStatus::Failed, writeRes.error().message));
                return writeRes.error();
            }

            const QFileInfo fi(record.path);
            const qint64 size = fi.size();
            const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
            const auto markRes = store.markWritten(writebackId, fileId, size, mtime);
            if (!markRes.ok()) {
                return markRes.error();
            }
            return { };
        });

    return std::make_unique<JobActionWorker>(future, std::move(done));
}

WritebackRevertJobHandler::WritebackRevertJobHandler(
    library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
    m_pool.setMaxThreadCount(kMaxWritebackThreads);
}

WritebackRevertJobHandler::~WritebackRevertJobHandler()
{
    m_pool.waitForDone();
}

QString WritebackRevertJobHandler::kind() const
{
    return QStringLiteral("library.writeback_revert");
}

int WritebackRevertJobHandler::maxInFlight() const
{
    return m_pool.maxThreadCount();
}

ai::TokenUsage WritebackRevertJobHandler::estimate(
    const QString & /*itemKey*/, const QJsonObject & /*params*/) const
{
    return ai::TokenUsage {
        .promptTokens = 0,
        .completionTokens = 0,
    };
}

std::unique_ptr<QObject> WritebackRevertJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    bool ok = false;
    const qint64 fileId = itemKey.toLongLong(&ok);
    const qint64 writebackId = params.value(QStringLiteral("writebackId")).toInteger();
    if (!ok || fileId <= 0 || writebackId <= 0) {
        done(core::Error {
            .code = QString(library::errc::kWritebackInvalid),
            .message = QStringLiteral("Invalid fileId or writebackId"),
            .detail = itemKey,
        });
        return nullptr;
    }

    auto future = QtConcurrent::run(
        &m_pool, [&db = m_db, &clock = m_clock, writebackId, fileId]() -> core::Result<void> {
            library::WritebackStore store(db, clock);
            const auto recRes = store.fileRecord(writebackId, fileId);
            if (!recRes.ok()) {
                return recRes.error();
            }
            const auto &record = recRes.value();
            if (record.status != library::WritebackFileStatus::Written) {
                return { };
            }

            const QFileInfo fi(record.path);
            if (!fi.exists() || fi.size() != record.writtenSize
                || fi.lastModified().toMSecsSinceEpoch() != record.writtenMtime) {
                const QString errMsg = QStringLiteral("File was modified after writeback");
                static_cast<void>(store.markFailed(
                    writebackId, fileId, library::WritebackFileStatus::RevertFailed, errMsg));
                return core::Error {
                    .code = QString(library::errc::kWritebackRevertFailed),
                    .message = errMsg,
                    .detail = record.path,
                };
            }

            const auto doc = QJsonDocument::fromJson(record.snapshot.toUtf8());
            const auto snapOpt = library::TagSnapshot::fromJson(doc.object());
            if (!snapOpt.has_value()) {
                const QString errMsg = QStringLiteral("Invalid tag snapshot JSON");
                static_cast<void>(store.markFailed(
                    writebackId, fileId, library::WritebackFileStatus::RevertFailed, errMsg));
                return core::Error {
                    .code = QString(library::errc::kWritebackRevertFailed),
                    .message = errMsg,
                    .detail = record.path,
                };
            }

            const auto restoreRes = library::TagWriter::restore(record.path, snapOpt.value());
            if (!restoreRes.ok()) {
                static_cast<void>(store.markFailed(writebackId, fileId,
                    library::WritebackFileStatus::RevertFailed, restoreRes.error().message));
                return restoreRes.error();
            }

            const auto markRes = store.markReverted(writebackId, fileId);
            if (!markRes.ok()) {
                return markRes.error();
            }
            return { };
        });

    return std::make_unique<JobActionWorker>(future, std::move(done));
}

} // namespace linernotes::butler

#include "WritebackJobHandler.moc"
