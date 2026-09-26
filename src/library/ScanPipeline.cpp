// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ScanPipeline.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QThreadPool>

#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/FileFingerprint.h>
#include <library/LibraryLogging.h>
#include <library/SearchIndex.h>
#include <library/TagReader.h>

namespace linernotes::library::detail {

FolderCoverCache::FolderCoverCache(CoverStore *store)
    : m_store(store)
{
}

FolderCoverEntry FolderCoverCache::findAndIngest(const QString &dirPath)
{
    if (m_store == nullptr) {
        return { };
    }

    {
        const std::scoped_lock lock(m_mutex);
        const auto it = m_cache.constFind(dirPath);
        if (it != m_cache.constEnd()) {
            return it.value();
        }
    }

    FolderCoverEntry entry = doFindAndIngest(dirPath);

    {
        const std::scoped_lock lock(m_mutex);
        m_cache.insert(dirPath, entry);
    }
    return entry;
}

FolderCoverEntry FolderCoverCache::doFindAndIngest(const QString &dirPath)
{
    static const QStringList s_baseNames = {
        QStringLiteral("cover"),
        QStringLiteral("folder"),
        QStringLiteral("front"),
        QStringLiteral("albumart"),
        QStringLiteral("album"),
    };
    static const QStringList s_extensions = {
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("webp"),
    };

    const QDir dir(dirPath);
    const QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::Readable);
    if (fileList.isEmpty()) {
        return { };
    }

    QString chosenFilePath;
    bool found = false;
    for (const auto &baseName : s_baseNames) {
        for (const auto &ext : s_extensions) {
            const QString expectedName = baseName + u'.' + ext;
            for (const auto &fi : fileList) {
                if (fi.fileName().compare(expectedName, Qt::CaseInsensitive) == 0) {
                    chosenFilePath = fi.absoluteFilePath();
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (found) {
            break;
        }
    }

    if (!found || chosenFilePath.isEmpty()) {
        return { };
    }

    QFile file(chosenFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCDebug(lcLibrary) << "Failed to open folder cover image:" << chosenFilePath;
        return { .found = true, .filePath = chosenFilePath, .info = std::nullopt };
    }

    const QByteArray data = file.readAll();
    const auto ingestRes = m_store->ingest(data);
    if (!ingestRes.ok()) {
        qCDebug(lcLibrary) << "Folder cover decode failed for" << chosenFilePath << ":"
                           << ingestRes.error().toString();
        return { .found = true, .filePath = chosenFilePath, .info = std::nullopt };
    }

    return FolderCoverEntry {
        .found = true,
        .filePath = chosenFilePath,
        .info = ingestRes.value(),
    };
}

CoverInfo extractCover(const QString &filePath, const TagReadResult &tagRes, CoverStore *coverStore,
    const std::shared_ptr<FolderCoverCache> &folderCoverCache)
{
    CoverInfo cover;
    if (coverStore == nullptr) {
        return cover;
    }

    if (tagRes.frontCover.has_value()) {
        const auto &pic = tagRes.frontCover.value();
        const auto ingestRes = coverStore->ingest(pic.data);
        if (ingestRes.ok()) {
            cover.source = CoverInfo::Source::Embedded;
            cover.info = ingestRes.value();
            if (!pic.mimeType.isEmpty()) {
                cover.info.mime = pic.mimeType;
            }
        } else {
            qCDebug(lcLibrary) << "Embedded cover ingest failed for" << filePath << ":"
                               << ingestRes.error().toString();
        }
    } else {
        const QString dirPath = QFileInfo(filePath).absolutePath();
        const auto folderCover = folderCoverCache->findAndIngest(dirPath);
        if (folderCover.found && folderCover.info.has_value()) {
            cover.source = CoverInfo::Source::Folder;
            cover.sourcePath = folderCover.filePath;
            cover.info = folderCover.info.value();
        }
    }
    return cover;
}

namespace {

void runReaderWorker(const QList<ScannedFile> &filesToRead,
    const QHash<QString, DbFile> &dbFilesByPath, BoundedQueue<FileReadResult> &resultQueue,
    CoverStore *coverStore, const std::shared_ptr<FolderCoverCache> &folderCoverCache,
    const std::atomic<bool> &cancelled, std::atomic<int> &activeWorkers,
    std::atomic<size_t> &nextIndex)
{
    const auto totalFiles = static_cast<size_t>(filesToRead.size());
    while (!cancelled.load()) {
        const size_t idx = nextIndex.fetch_add(1);
        if (idx >= totalFiles) {
            break;
        }
        const auto &item = filesToRead.at(static_cast<qsizetype>(idx));
        FileReadResult res;
        res.scanned = item;
        if (dbFilesByPath.contains(item.path)) {
            res.isNew = false;
            res.existingDbFile = dbFilesByPath.value(item.path);
        } else {
            res.isNew = true;
        }

        res.tagResult = TagReader::read(item.path);
        res.fingerprintResult = FileFingerprint::compute(item.path);

        if (res.tagResult.ok()) {
            res.cover
                = extractCover(item.path, res.tagResult.value(), coverStore, folderCoverCache);
        }

        resultQueue.push(std::move(res));
    }
    if (activeWorkers.fetch_sub(1) == 1) {
        resultQueue.stop();
    }
}

void startReaderWorkers(QThreadPool &pool, const QList<ScannedFile> &filesToRead,
    const QHash<QString, DbFile> &dbFilesByPath, BoundedQueue<FileReadResult> &resultQueue,
    int workerCount, CoverStore *coverStore, const std::atomic<bool> &cancelled,
    std::atomic<int> &activeWorkers, std::atomic<size_t> &nextIndex)
{
    auto folderCoverCache = std::make_shared<FolderCoverCache>(coverStore);

    for (int i = 0; i < workerCount; ++i) {
        pool.start([&filesToRead, &dbFilesByPath, &resultQueue, coverStore, folderCoverCache,
                       &cancelled, &activeWorkers, &nextIndex]() {
            runReaderWorker(filesToRead, dbFilesByPath, resultQueue, coverStore, folderCoverCache,
                cancelled, activeWorkers, nextIndex);
        });
    }
}

} // namespace

core::Result<void> processWriterQueue(const QSqlDatabase &db,
    BoundedQueue<FileReadResult> &resultQueue, int batchSize, int totalToRead,
    const std::function<qint64()> &getNow,
    const std::function<void(const ScanProgress &)> &onProgress, ScanStats &stats)
{
    ScanWriter writer(db);
    EntityLinker entityLinker(db, getNow);
    std::unique_ptr<Transaction> tx;
    int batchCount = 0;
    int readDone = 0;

    QElapsedTimer progressTimer;
    progressTimer.start();
    int lastProgressDone = 0;
    qint64 lastProgressMs = 0;

    FileReadResult res;
    while (resultQueue.pop(res)) {
        if (!tx) {
            tx = std::make_unique<Transaction>(db);
        }

        const qint64 now = getNow();
        if (res.tagResult.ok()) {
            writer.writeSuccessfulFile(res, entityLinker, stats, now);
        } else {
            writer.writeFailedFile(res, stats, now);
        }

        batchCount++;
        readDone++;

        if (batchCount >= batchSize) {
            SearchIndex searchIndex(db);
            const auto flushRes = searchIndex.flushDirty();
            if (!flushRes.ok()) {
                qCWarning(lcLibrary)
                    << "Batch search index flush failed:" << flushRes.error().toString();
                resultQueue.stop();
                return flushRes.error();
            }
            auto commitRes = tx->commit();
            if (!commitRes.ok()) {
                qCWarning(lcLibrary) << "Batch commit failed:" << commitRes.error().toString();
                resultQueue.stop();
                return commitRes.error();
            }
            tx.reset();
            batchCount = 0;
        }

        const qint64 elapsedMs = progressTimer.elapsed();
        if (readDone - lastProgressDone >= 100 || elapsedMs - lastProgressMs >= 200
            || readDone == totalToRead) {
            onProgress(ScanProgress {
                .phase = ScanProgress::Phase::Reading,
                .done = readDone,
                .total = totalToRead,
            });
            lastProgressDone = readDone;
            lastProgressMs = elapsedMs;
        }
    }

    if (tx && tx->isActive()) {
        SearchIndex searchIndex(db);
        const auto flushRes = searchIndex.flushDirty();
        if (!flushRes.ok()) {
            qCWarning(lcLibrary) << "Final batch search index flush failed:"
                                 << flushRes.error().toString();
            return flushRes.error();
        }
        auto commitRes = tx->commit();
        if (!commitRes.ok()) {
            qCWarning(lcLibrary) << "Final batch commit failed:" << commitRes.error().toString();
            return commitRes.error();
        }
        tx.reset();
    }

    return { };
}

core::Result<void> executeReadAndWritePipeline(const QSqlDatabase &db,
    const QList<ScannedFile> &filesToRead, const QHash<QString, DbFile> &dbFilesByPath,
    int batchSize, int maxThreads, CoverStore *coverStore, std::atomic<bool> &cancelled,
    const std::function<qint64()> &getNow,
    const std::function<void(const ScanProgress &)> &onProgress, ScanStats &stats)
{
    BoundedQueue<FileReadResult> resultQueue(std::max(100, 2 * batchSize));
    const auto workerCount = static_cast<int>(std::min<qsizetype>(maxThreads, filesToRead.size()));

    std::atomic<size_t> nextIndex { 0 };
    std::atomic<int> activeWorkers { workerCount };

    QThreadPool pool;
    pool.setMaxThreadCount(maxThreads);

    startReaderWorkers(pool, filesToRead, dbFilesByPath, resultQueue, workerCount, coverStore,
        cancelled, activeWorkers, nextIndex);

    const auto writeRes = processWriterQueue(db, resultQueue, batchSize,
        static_cast<int>(filesToRead.size()), getNow, onProgress, stats);

    if (!writeRes.ok()) {
        cancelled.store(true);
        resultQueue.stop();
    }

    pool.waitForDone();
    return writeRes;
}

} // namespace linernotes::library::detail
