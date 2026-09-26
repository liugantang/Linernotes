// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Scanner.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>

#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>
#include <library/LibraryRoots.h>
#include <library/ScanDiff.h>
#include <library/ScanPipeline.h>
#include <library/SearchIndex.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace linernotes::library {

namespace {

void performCleanupOrphans(const QSqlDatabase &db, CoverStore *coverStore,
    const std::function<qint64()> &getNow, bool isCancelled, ScanStats &stats)
{
    EntityLinker linker(db, getNow);
    Transaction tx(db);
    auto removeRes = linker.removeOrphans();
    if (removeRes.ok()) {
        auto commitRes = tx.commit();
        if (commitRes.ok()) {
            stats.albumsRemoved = removeRes.value().first;
            stats.artistsRemoved = removeRes.value().second;
        } else {
            qCWarning(lcLibrary) << "Failed to commit removeOrphans transaction:"
                                 << commitRes.error().toString();
        }
    } else {
        qCWarning(lcLibrary) << "Failed to remove orphans:" << removeRes.error().toString();
    }

    if (!isCancelled && coverStore != nullptr) {
        QSqlQuery qHashes(db);
        if (qHashes.exec(QStringLiteral("SELECT hash FROM covers"))) {
            QSet<QString> keepHashes;
            while (qHashes.next()) {
                keepHashes.insert(qHashes.value(0).toString());
            }
            coverStore->prune(keepHashes);
        }
    }
}

struct WalkAndDiffResult {
    QList<detail::ScannedFile> filesToRead;
    QHash<QString, detail::DbFile> dbFilesByPath;
    bool cancelled = false;
};

core::Result<WalkAndDiffResult> scanWalkAndDiff(Database &database, const QSqlDatabase &db,
    const std::atomic<bool> &cancelled, const QStringList &dirs,
    const std::function<void(const ScanProgress &)> &onProgress,
    const std::function<qint64()> &getNow, ScanStats &stats)
{
    const LibraryRoots libraryRoots(database);
    const auto rootsListRes = libraryRoots.list();
    if (!rootsListRes.ok()) {
        return rootsListRes.error();
    }

    QList<LibraryRoot> enabledRoots;
    for (const auto &r : rootsListRes.value()) {
        if (r.enabled) {
            enabledRoots.append(r);
        }
    }

    const auto targets = detail::collectWalkTargets(enabledRoots, dirs);

    QList<detail::ScannedFile> walkedFiles;
    const bool walkOk = detail::walkAllTargets(targets, cancelled, onProgress, walkedFiles);

    QHash<QString, detail::ScannedFile> walkedMap;
    for (const auto &f : walkedFiles) {
        walkedMap.insert(f.path, f);
    }
    stats.found = static_cast<int>(walkedMap.size());

    if (!walkOk || cancelled.load()) {
        qCInfo(lcLibrary) << "Scan cancelled during walk: found=" << stats.found;
        return WalkAndDiffResult { .filesToRead = { }, .dbFilesByPath = { }, .cancelled = true };
    }

    const auto dbFilesRes = detail::queryDbFiles(db, enabledRoots, dirs);
    if (!dbFilesRes.ok()) {
        return dbFilesRes.error();
    }
    const auto &dbFilesByPath = dbFilesRes.value();

    QList<detail::ScannedFile> filesToRead;
    QList<qint64> restoredFileIds;
    QList<qint64> missingFileIds;

    detail::classifyFiles(
        walkedMap, dbFilesByPath, filesToRead, restoredFileIds, missingFileIds, stats);

    if (cancelled.load()) {
        qCInfo(lcLibrary) << "Scan cancelled before missing update: found=" << stats.found;
        return WalkAndDiffResult { .filesToRead = { }, .dbFilesByPath = { }, .cancelled = true };
    }

    const auto applyRes
        = detail::applyMissingAndRestored(db, missingFileIds, restoredFileIds, getNow());
    if (!applyRes.ok()) {
        return applyRes.error();
    }

    return WalkAndDiffResult {
        .filesToRead = std::move(filesToRead),
        .dbFilesByPath = dbFilesByPath,
        .cancelled = false,
    };
}

core::Result<void> scanPipeline(const QSqlDatabase &db,
    const QList<detail::ScannedFile> &filesToRead,
    const QHash<QString, detail::DbFile> &dbFilesByPath, const Scanner::Options &options,
    std::atomic<bool> &cancelled, const std::function<qint64()> &getNow,
    const std::function<void(const ScanProgress &)> &onProgress, ScanStats &stats)
{
    int maxThreads = options.maxThreads > 0 ? options.maxThreads : QThread::idealThreadCount();
    maxThreads = std::max(maxThreads, 1);
    const int batchSize = std::max(1, options.batchSize);

    return detail::executeReadAndWritePipeline(db, filesToRead, dbFilesByPath, batchSize,
        maxThreads, options.coverStore, cancelled, getNow, onProgress, stats);
}

void finalizeScan(
    ScanStats &stats, qint64 elapsedMs, const std::function<void(const ScanStats &)> &onFinished)
{
    stats.elapsedMs = elapsedMs;
    if (stats.error.isEmpty()) {
        qCInfo(lcLibrary) << "Scan finished: found=" << stats.found << "added=" << stats.added
                          << "updated=" << stats.updated << "unchanged=" << stats.unchanged
                          << "moved=" << stats.moved << "missing=" << stats.missing
                          << "restored=" << stats.restored << "failed=" << stats.failed
                          << "albumsRemoved=" << stats.albumsRemoved
                          << "artistsRemoved=" << stats.artistsRemoved
                          << "cancelled=" << stats.cancelled << "elapsedMs=" << stats.elapsedMs;
    }
    onFinished(stats);
}

} // namespace

Scanner::Scanner(Database &db, Options options, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_options(std::move(options))
{
    qRegisterMetaType<linernotes::library::ScanStats>();
    qRegisterMetaType<linernotes::library::ScanProgress>();
}

Scanner::~Scanner()
{
    cancel();
    if (m_workerThread != nullptr) {
        if (m_workerThread->isRunning()) {
            m_workerThread->wait();
        }
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

bool Scanner::start()
{
    return startPaths({ });
}

bool Scanner::startPaths(const QStringList &dirs)
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return false;
    }
    m_cancelled.store(false);

    if (m_workerThread != nullptr) {
        if (m_workerThread->isRunning()) {
            m_workerThread->wait();
        }
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    m_workerThread = QThread::create([this, dirs]() {
        [[maybe_unused]] auto res = doScan(dirs);
        m_running.store(false);
    });
    m_workerThread->start();
    return true;
}

void Scanner::cancel()
{
    m_cancelled.store(true);
}

bool Scanner::isRunning() const
{
    return m_running.load();
}

core::Result<ScanStats> Scanner::scanBlocking(const QStringList &dirs)
{
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return core::Error {
            .code = QString(errc::kDbTransaction),
            .message = QStringLiteral("Scan is already in progress"),
            .detail = QString(),
        };
    }
    m_cancelled.store(false);

    auto result = doScan(dirs);
    m_running.store(false);
    return result;
}

qint64 Scanner::getNow() const
{
    if (m_options.nowMs) {
        return m_options.nowMs();
    }
    return QDateTime::currentMSecsSinceEpoch();
}

core::Result<ScanStats> Scanner::doScan(const QStringList &dirs)
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    ScanStats stats;

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        stats.error = connRes.error().toString();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return connRes.error();
    }
    const QSqlDatabase &db = connRes.value();

    auto emitProgress = [this](const ScanProgress &p) { emit progress(p); };
    auto nowFn = [this]() { return getNow(); };

    const auto diffRes = scanWalkAndDiff(m_db, db, m_cancelled, dirs, emitProgress, nowFn, stats);
    if (!diffRes.ok()) {
        stats.error = diffRes.error().toString();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return diffRes.error();
    }

    const auto &diff = diffRes.value();
    if (diff.cancelled || m_cancelled.load()) {
        stats.cancelled = true;
        performCleanupOrphans(db, m_options.coverStore, nowFn, true, stats);
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return stats;
    }

    if (diff.filesToRead.isEmpty()) {
        performCleanupOrphans(
            db, m_options.coverStore, nowFn, stats.cancelled || m_cancelled.load(), stats);
        {
            Transaction tx(db);
            SearchIndex searchIndex(db);
            const auto flushRes = searchIndex.flushDirty();
            if (flushRes.ok()) {
                const auto commitRes = tx.commit();
                if (!commitRes.ok()) {
                    qCWarning(lcLibrary)
                        << "Failed to commit search index flush:" << commitRes.error().toString();
                }
            }
        }
        emit progress(ScanProgress {
            .phase = ScanProgress::Phase::Finishing,
            .done = 0,
            .total = 0,
        });
        finalizeScan(stats, totalTimer.elapsed(), [this](const ScanStats &s) { emit finished(s); });
        return stats;
    }

    const auto pipelineRes = scanPipeline(db, diff.filesToRead, diff.dbFilesByPath, m_options,
        m_cancelled, nowFn, emitProgress, stats);

    if (!pipelineRes.ok()) {
        stats.error = pipelineRes.error().toString();
        performCleanupOrphans(
            db, m_options.coverStore, nowFn, stats.cancelled || m_cancelled.load(), stats);
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return pipelineRes.error();
    }

    stats.cancelled = m_cancelled.load();
    performCleanupOrphans(
        db, m_options.coverStore, nowFn, stats.cancelled || m_cancelled.load(), stats);

    emit progress(ScanProgress {
        .phase = ScanProgress::Phase::Finishing,
        .done = static_cast<int>(diff.filesToRead.size()),
        .total = static_cast<int>(diff.filesToRead.size()),
    });

    finalizeScan(stats, totalTimer.elapsed(), [this](const ScanStats &s) { emit finished(s); });
    return stats;
}

} // namespace linernotes::library
