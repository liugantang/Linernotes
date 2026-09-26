// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Scanner.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QThreadPool>

#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/DirectoryWalker.h>
#include <library/EntityLinker.h>
#include <library/Errors.h>
#include <library/FileFingerprint.h>
#include <library/LibraryLogging.h>
#include <library/LibraryRoots.h>
#include <library/SearchIndex.h>
#include <library/TagReader.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace linernotes::library {

namespace {

template <typename T> class BoundedQueue {
public:
    explicit BoundedQueue(size_t maxSize)
        : m_maxSize(std::max<size_t>(1, maxSize))
    {
    }

    void push(T item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvNotFull.wait(lock, [this]() { return m_queue.size() < m_maxSize || m_stopped; });
        if (m_stopped) {
            return;
        }
        m_queue.push_back(std::move(item));
        m_cvNotEmpty.notify_one();
    }

    bool pop(T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvNotEmpty.wait(lock, [this]() { return !m_queue.empty() || m_stopped; });
        if (m_queue.empty()) {
            return false;
        }
        item = std::move(m_queue.front());
        m_queue.pop_front();
        m_cvNotFull.notify_one();
        return true;
    }

    void stop()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopped = true;
        }
        m_cvNotFull.notify_all();
        m_cvNotEmpty.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cvNotFull;
    std::condition_variable m_cvNotEmpty;
    std::deque<T> m_queue;
    size_t m_maxSize;
    bool m_stopped = false;
};

struct ScannedFile {
    QString path;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    qint64 rootId = 0;
};

struct DbFile {
    qint64 id = 0;
    qint64 rootId = 0;
    QString path;
    qint64 size = 0;
    qint64 mtime = 0;
    QString contentHash;
    std::optional<qint64> missingSince;
};

struct CoverInfo {
    enum class Source : std::uint8_t { None, Embedded, Folder };
    Source source = Source::None;
    QString sourcePath;
    CoverStore::Info info;
};

// Known limitation: When audio files have not changed (mtime/size unchanged),
// the scanner will not re-check folder covers. Therefore, a newly added cover.jpg
// in an existing folder will only take effect when an audio file in that folder
// changes or is re-scanned (can be improved with file watching in task 2.8).

struct FolderCoverEntry {
    bool found = false;
    QString filePath;
    std::optional<CoverStore::Info> info;
};

class FolderCoverCache {
public:
    explicit FolderCoverCache(CoverStore *store)
        : m_store(store)
    {
    }

    FolderCoverEntry findAndIngest(const QString &dirPath)
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

private:
    FolderCoverEntry doFindAndIngest(const QString &dirPath)
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

    CoverStore *m_store = nullptr;
    std::mutex m_mutex;
    QHash<QString, FolderCoverEntry> m_cache;
};

struct FileReadResult {
    ScannedFile scanned;
    bool isNew = true;
    DbFile existingDbFile;
    core::Result<TagReadResult> tagResult = core::Error { };
    core::Result<QString> fingerprintResult = core::Error { };
    CoverInfo cover;
};

struct WalkTarget {
    QString rootPath;
    QString walkDir;
    qint64 rootId = 0;
    QStringList excludes;
};

QList<WalkTarget> collectWalkTargets(
    const QList<LibraryRoot> &enabledRoots, const QStringList &dirs)
{
    QList<WalkTarget> targets;
    if (dirs.isEmpty()) {
        for (const auto &r : enabledRoots) {
            targets.append(WalkTarget {
                .rootPath = r.path,
                .walkDir = r.path,
                .rootId = r.id,
                .excludes = r.excludes,
            });
        }
    } else {
        for (const auto &d : dirs) {
            const QString cleanD = QDir::cleanPath(d);
            bool found = false;
            for (const auto &r : enabledRoots) {
                if (cleanD == r.path || cleanD.startsWith(r.path + u'/')) {
                    targets.append(WalkTarget {
                        .rootPath = r.path,
                        .walkDir = cleanD,
                        .rootId = r.id,
                        .excludes = r.excludes,
                    });
                    found = true;
                    break;
                }
            }
            if (!found) {
                qCWarning(lcLibrary)
                    << "Directory" << cleanD << "is not under any enabled library root";
            }
        }
    }
    return targets;
}

bool walkAllTargets(const QList<WalkTarget> &targets, const std::atomic<bool> &cancelled,
    const std::function<void(const ScanProgress &)> &onProgress, QList<ScannedFile> &walkedFiles)
{
    const auto totalTargets = static_cast<int>(targets.size());
    int currentTargetIdx = 0;

    for (const auto &target : targets) {
        if (cancelled.load()) {
            return false;
        }

        WalkOptions walkOpts;
        walkOpts.excludes = target.excludes;
        walkOpts.isCancelled = [&cancelled]() { return cancelled.load(); };

        bool walkCancelled = false;
        const auto files
            = DirectoryWalker::walk(target.rootPath, target.walkDir, walkOpts, &walkCancelled);
        for (const auto &f : files) {
            walkedFiles.append(ScannedFile {
                .path = f.path,
                .size = f.size,
                .mtimeMs = f.mtimeMs,
                .rootId = target.rootId,
            });
        }

        currentTargetIdx++;
        onProgress(ScanProgress {
            .phase = ScanProgress::Phase::Walking,
            .done = currentTargetIdx,
            .total = totalTargets,
        });

        if (walkCancelled || cancelled.load()) {
            return false;
        }
    }
    return true;
}

core::Result<QHash<QString, DbFile>> queryDbFiles(
    const QSqlDatabase &db, const QList<LibraryRoot> &enabledRoots, const QStringList &dirs)
{
    QHash<QString, DbFile> dbFilesByPath;
    if (enabledRoots.isEmpty()) {
        return dbFilesByPath;
    }

    QStringList rootIdStrs;
    for (const auto &r : enabledRoots) {
        rootIdStrs.append(QString::number(r.id));
    }
    const QString sql = QStringLiteral(
        "SELECT id, root_id, path, size, mtime, content_hash, missing_since FROM files "
        "WHERE root_id IN (%1)")
                            .arg(rootIdStrs.join(u','));

    QSqlQuery q(db);
    if (!q.exec(sql)) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = sql,
        };
    }

    while (q.next()) {
        const QString path = q.value(2).toString();
        bool inScope = dirs.isEmpty();
        if (!inScope) {
            for (const auto &d : dirs) {
                const QString cleanD = QDir::cleanPath(d);
                if (path == cleanD || path.startsWith(cleanD + u'/')) {
                    inScope = true;
                    break;
                }
            }
        }

        if (inScope) {
            DbFile f;
            f.id = q.value(0).toLongLong();
            f.rootId = q.value(1).toLongLong();
            f.path = path;
            f.size = q.value(3).toLongLong();
            f.mtime = q.value(4).toLongLong();
            f.contentHash = q.value(5).toString();
            if (!q.value(6).isNull()) {
                f.missingSince = q.value(6).toLongLong();
            }
            dbFilesByPath.insert(f.path, f);
        }
    }

    return dbFilesByPath;
}

void classifyFiles(const QHash<QString, ScannedFile> &walkedMap,
    const QHash<QString, DbFile> &dbFilesByPath, QList<ScannedFile> &filesToRead,
    QList<qint64> &restoredFileIds, QList<qint64> &missingFileIds, ScanStats &stats)
{
    for (auto it = walkedMap.constBegin(); it != walkedMap.constEnd(); ++it) {
        const auto &scanned = it.value();
        if (dbFilesByPath.contains(scanned.path)) {
            const auto &dbFile = dbFilesByPath.value(scanned.path);
            if (scanned.size == dbFile.size && scanned.mtimeMs == dbFile.mtime) {
                if (dbFile.missingSince.has_value()) {
                    restoredFileIds.append(dbFile.id);
                    stats.restored++;
                } else {
                    stats.unchanged++;
                }
            } else {
                filesToRead.append(scanned);
            }
        } else {
            filesToRead.append(scanned);
        }
    }

    for (auto it = dbFilesByPath.constBegin(); it != dbFilesByPath.constEnd(); ++it) {
        const auto &dbFile = it.value();
        if (!walkedMap.contains(dbFile.path)) {
            if (!dbFile.missingSince.has_value()) {
                missingFileIds.append(dbFile.id);
                stats.missing++;
            }
        }
    }
}

core::Result<void> applyMissingAndRestored(const QSqlDatabase &db,
    const QList<qint64> &missingFileIds, const QList<qint64> &restoredFileIds, qint64 now)
{
    if (missingFileIds.isEmpty() && restoredFileIds.isEmpty()) {
        return { };
    }

    Transaction tx(db);
    if (!missingFileIds.isEmpty()) {
        QSqlQuery qMiss(db);
        qMiss.prepare(QStringLiteral("UPDATE files SET missing_since = ? WHERE id = ?"));
        for (const qint64 id : missingFileIds) {
            qMiss.bindValue(0, now);
            qMiss.bindValue(1, id);
            if (!qMiss.exec()) {
                tx.rollback();
                return core::Error {
                    .code = QString(errc::kDbQuery),
                    .message = qMiss.lastError().text(),
                    .detail = QString::number(id),
                };
            }
        }
    }

    if (!restoredFileIds.isEmpty()) {
        QSqlQuery qRest(db);
        qRest.prepare(QStringLiteral("UPDATE files SET missing_since = NULL WHERE id = ?"));
        for (const qint64 id : restoredFileIds) {
            qRest.bindValue(0, id);
            if (!qRest.exec()) {
                tx.rollback();
                return core::Error {
                    .code = QString(errc::kDbQuery),
                    .message = qRest.lastError().text(),
                    .detail = QString::number(id),
                };
            }
        }
    }

    return tx.commit();
}

struct WriterStatements {
    QSqlQuery savepointStmt;
    QSqlQuery releaseStmt;
    QSqlQuery rollbackToStmt;
    QSqlQuery findMissingStmt;
    QSqlQuery updateMovedFileStmt;
    QSqlQuery insertNewFileStmt;
    QSqlQuery updateFileStmt;
    QSqlQuery insertNewFileFailedStmt;
    QSqlQuery updateFileFailedStmt;
    QSqlQuery findTrackStmt;
    QSqlQuery insertTrackStmt;
    QSqlQuery deleteRawTagsStmt;
    QSqlQuery insertRawTagStmt;
    QSqlQuery updateTrackTagsReadAtStmt;
    QSqlQuery findCoverStmt;
    QSqlQuery insertCoverStmt;

    explicit WriterStatements(const QSqlDatabase &db)
        : savepointStmt(db)
        , releaseStmt(db)
        , rollbackToStmt(db)
        , findMissingStmt(db)
        , updateMovedFileStmt(db)
        , insertNewFileStmt(db)
        , updateFileStmt(db)
        , insertNewFileFailedStmt(db)
        , updateFileFailedStmt(db)
        , findTrackStmt(db)
        , insertTrackStmt(db)
        , deleteRawTagsStmt(db)
        , insertRawTagStmt(db)
        , updateTrackTagsReadAtStmt(db)
        , findCoverStmt(db)
        , insertCoverStmt(db)
    {
        savepointStmt.prepare(QStringLiteral("SAVEPOINT scan_file;"));
        releaseStmt.prepare(QStringLiteral("RELEASE scan_file;"));
        rollbackToStmt.prepare(QStringLiteral("ROLLBACK TO scan_file;"));

        findMissingStmt.prepare(QStringLiteral(
            "SELECT id, root_id, path FROM files WHERE missing_since IS NOT NULL AND size = ? "
            "AND content_hash = ? ORDER BY missing_since ASC LIMIT 1"));
        updateMovedFileStmt.prepare(QStringLiteral(
            "UPDATE files SET path = ?, root_id = ?, mtime = ?, missing_since = NULL WHERE id = "
            "?"));
        insertNewFileStmt.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, content_hash, container, codec, "
            "duration_ms, bitrate, sample_rate, bit_depth, channels, has_embedded_cover, "
            "cover_id, scan_error, first_seen_at, scanned_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?)"));
        updateFileStmt.prepare(QStringLiteral(
            "UPDATE files SET root_id = ?, size = ?, mtime = ?, content_hash = ?, container = ?, "
            "codec = ?, duration_ms = ?, bitrate = ?, sample_rate = ?, bit_depth = ?, channels "
            "= ?, has_embedded_cover = ?, cover_id = ?, scan_error = NULL, scanned_at = ?, "
            "missing_since = NULL "
            "WHERE id = ?"));
        insertNewFileFailedStmt.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, content_hash, scan_error, "
            "first_seen_at, scanned_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        updateFileFailedStmt.prepare(QStringLiteral(
            "UPDATE files SET root_id = ?, size = ?, mtime = ?, content_hash = ?, scan_error = "
            "?, scanned_at = ?, missing_since = NULL WHERE id = ?"));
        findTrackStmt.prepare(QStringLiteral(
            "SELECT id FROM tracks WHERE file_id = ? AND cue_index IS NULL LIMIT 1"));
        insertTrackStmt.prepare(
            QStringLiteral("INSERT INTO tracks (file_id, cue_index, tags_read_at, created_at) "
                           "VALUES (?, NULL, ?, ?)"));
        deleteRawTagsStmt.prepare(QStringLiteral("DELETE FROM raw_tags WHERE track_id = ?"));
        insertRawTagStmt.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value, raw_bytes, "
            "raw_encoding) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
        updateTrackTagsReadAtStmt.prepare(
            QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?"));
        findCoverStmt.prepare(QStringLiteral("SELECT id FROM covers WHERE hash = ? LIMIT 1"));
        insertCoverStmt.prepare(QStringLiteral(
            "INSERT INTO covers (hash, mime, width, height, source, source_path, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(hash) DO NOTHING"));
    }
};

enum class FileOp : std::uint8_t { Added, Updated, Moved };
enum class MoveLookupResult : std::uint8_t { FoundAndMoved, NotFound, Error };

MoveLookupResult tryResolveMovedRecord(
    const FileReadResult &res, WriterStatements &stmts, qint64 &outFileId)
{
    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    if (contentHash.isEmpty()) {
        return MoveLookupResult::NotFound;
    }

    stmts.findMissingStmt.bindValue(0, res.scanned.size);
    stmts.findMissingStmt.bindValue(1, contentHash);
    if (!stmts.findMissingStmt.exec()) {
        return MoveLookupResult::Error;
    }
    if (!stmts.findMissingStmt.next()) {
        return MoveLookupResult::NotFound;
    }

    const qint64 missingId = stmts.findMissingStmt.value(0).toLongLong();
    stmts.updateMovedFileStmt.bindValue(0, res.scanned.path);
    stmts.updateMovedFileStmt.bindValue(1, res.scanned.rootId);
    stmts.updateMovedFileStmt.bindValue(2, res.scanned.mtimeMs);
    stmts.updateMovedFileStmt.bindValue(3, missingId);
    if (!stmts.updateMovedFileStmt.exec()) {
        return MoveLookupResult::Error;
    }
    outFileId = missingId;
    return MoveLookupResult::FoundAndMoved;
}

std::optional<qint64> resolveCoverId(const CoverInfo &cover, WriterStatements &stmts, qint64 now)
{
    if (cover.source == CoverInfo::Source::None || cover.info.hash.isEmpty()) {
        return std::nullopt;
    }

    const QString sourceStr = (cover.source == CoverInfo::Source::Embedded)
        ? QStringLiteral("embedded")
        : QStringLiteral("folder");
    const QVariant sourcePathVal
        = (cover.source == CoverInfo::Source::Folder && !cover.sourcePath.isEmpty())
        ? QVariant(cover.sourcePath)
        : QVariant();

    stmts.insertCoverStmt.bindValue(0, cover.info.hash);
    stmts.insertCoverStmt.bindValue(1, cover.info.mime);
    stmts.insertCoverStmt.bindValue(
        2, cover.info.width > 0 ? QVariant(cover.info.width) : QVariant());
    stmts.insertCoverStmt.bindValue(
        3, cover.info.height > 0 ? QVariant(cover.info.height) : QVariant());
    stmts.insertCoverStmt.bindValue(4, sourceStr);
    stmts.insertCoverStmt.bindValue(5, sourcePathVal);
    stmts.insertCoverStmt.bindValue(6, now);
    if (!stmts.insertCoverStmt.exec()) {
        qCWarning(lcLibrary) << "Failed to insert cover:"
                             << stmts.insertCoverStmt.lastError().text();
        return std::nullopt;
    }

    stmts.findCoverStmt.bindValue(0, cover.info.hash);
    if (!stmts.findCoverStmt.exec() || !stmts.findCoverStmt.next()) {
        qCWarning(lcLibrary) << "Failed to find cover id after insert:"
                             << stmts.findCoverStmt.lastError().text();
        return std::nullopt;
    }

    return stmts.findCoverStmt.value(0).toLongLong();
}

bool insertNewRecord(
    const FileReadResult &res, WriterStatements &stmts, qint64 now, qint64 &outFileId)
{
    const auto &tagRes = res.tagResult.value();
    const auto &audio = tagRes.audio;
    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    const std::optional<qint64> coverId = resolveCoverId(res.cover, stmts, now);

    stmts.insertNewFileStmt.bindValue(0, res.scanned.rootId);
    stmts.insertNewFileStmt.bindValue(1, res.scanned.path);
    stmts.insertNewFileStmt.bindValue(2, res.scanned.size);
    stmts.insertNewFileStmt.bindValue(3, res.scanned.mtimeMs);
    stmts.insertNewFileStmt.bindValue(4, contentHash.isEmpty() ? QVariant() : contentHash);
    stmts.insertNewFileStmt.bindValue(5, audio.container);
    stmts.insertNewFileStmt.bindValue(6, audio.codec);
    stmts.insertNewFileStmt.bindValue(7, audio.durationMs);
    stmts.insertNewFileStmt.bindValue(8, audio.bitrate);
    stmts.insertNewFileStmt.bindValue(9, audio.sampleRate);
    stmts.insertNewFileStmt.bindValue(10, audio.bitDepth);
    stmts.insertNewFileStmt.bindValue(11, audio.channels);
    stmts.insertNewFileStmt.bindValue(12, tagRes.hasEmbeddedCover ? 1 : 0);
    stmts.insertNewFileStmt.bindValue(
        13, coverId.has_value() ? QVariant(coverId.value()) : QVariant());
    stmts.insertNewFileStmt.bindValue(14, now);
    stmts.insertNewFileStmt.bindValue(15, now);
    if (!stmts.insertNewFileStmt.exec()) {
        return false;
    }
    outFileId = stmts.insertNewFileStmt.lastInsertId().toLongLong();
    return true;
}

bool updateExistingRecord(
    const FileReadResult &res, WriterStatements &stmts, qint64 now, qint64 &outFileId)
{
    const auto &tagRes = res.tagResult.value();
    const auto &audio = tagRes.audio;
    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    const std::optional<qint64> coverId = resolveCoverId(res.cover, stmts, now);
    outFileId = res.existingDbFile.id;

    stmts.updateFileStmt.bindValue(0, res.scanned.rootId);
    stmts.updateFileStmt.bindValue(1, res.scanned.size);
    stmts.updateFileStmt.bindValue(2, res.scanned.mtimeMs);
    stmts.updateFileStmt.bindValue(3, contentHash.isEmpty() ? QVariant() : contentHash);
    stmts.updateFileStmt.bindValue(4, audio.container);
    stmts.updateFileStmt.bindValue(5, audio.codec);
    stmts.updateFileStmt.bindValue(6, audio.durationMs);
    stmts.updateFileStmt.bindValue(7, audio.bitrate);
    stmts.updateFileStmt.bindValue(8, audio.sampleRate);
    stmts.updateFileStmt.bindValue(9, audio.bitDepth);
    stmts.updateFileStmt.bindValue(10, audio.channels);
    stmts.updateFileStmt.bindValue(11, tagRes.hasEmbeddedCover ? 1 : 0);
    stmts.updateFileStmt.bindValue(
        12, coverId.has_value() ? QVariant(coverId.value()) : QVariant());
    stmts.updateFileStmt.bindValue(13, now);
    stmts.updateFileStmt.bindValue(14, outFileId);
    return stmts.updateFileStmt.exec();
}

bool writeTrackRecord(qint64 fileId, WriterStatements &stmts, qint64 now, qint64 &outTrackId)
{
    stmts.findTrackStmt.bindValue(0, fileId);
    if (!stmts.findTrackStmt.exec()) {
        return false;
    }
    if (stmts.findTrackStmt.next()) {
        outTrackId = stmts.findTrackStmt.value(0).toLongLong();
        return true;
    }

    stmts.insertTrackStmt.bindValue(0, fileId);
    stmts.insertTrackStmt.bindValue(1, now);
    stmts.insertTrackStmt.bindValue(2, now);
    if (!stmts.insertTrackStmt.exec()) {
        return false;
    }
    outTrackId = stmts.insertTrackStmt.lastInsertId().toLongLong();
    return true;
}

bool writeRawTags(qint64 trackId, const QList<RawTag> &tags, WriterStatements &stmts, qint64 now)
{
    stmts.deleteRawTagsStmt.bindValue(0, trackId);
    if (!stmts.deleteRawTagsStmt.exec()) {
        return false;
    }

    for (const auto &tag : tags) {
        stmts.insertRawTagStmt.bindValue(0, trackId);
        stmts.insertRawTagStmt.bindValue(1, tag.tagType);
        stmts.insertRawTagStmt.bindValue(2, tag.priority);
        stmts.insertRawTagStmt.bindValue(3, tag.key);
        stmts.insertRawTagStmt.bindValue(4, tag.ordinal);
        stmts.insertRawTagStmt.bindValue(5, tag.value);
        if (tag.rawBytes.isEmpty()) {
            stmts.insertRawTagStmt.bindValue(6, QVariant(QMetaType::fromType<QByteArray>()));
        } else {
            stmts.insertRawTagStmt.bindValue(6, tag.rawBytes);
        }
        if (tag.rawEncoding.isEmpty()) {
            stmts.insertRawTagStmt.bindValue(7, QVariant(QMetaType::fromType<QString>()));
        } else {
            stmts.insertRawTagStmt.bindValue(7, tag.rawEncoding);
        }
        if (!stmts.insertRawTagStmt.exec()) {
            return false;
        }
    }

    stmts.updateTrackTagsReadAtStmt.bindValue(0, now);
    stmts.updateTrackTagsReadAtStmt.bindValue(1, trackId);
    return stmts.updateTrackTagsReadAtStmt.exec();
}

bool writeSuccessfulFile(const FileReadResult &res, WriterStatements &stmts,
    EntityLinker &entityLinker, ScanStats &stats, qint64 now)
{
    if (!stmts.savepointStmt.exec()) {
        qCWarning(lcLibrary) << "Failed to create savepoint for" << res.scanned.path << ":"
                             << stmts.savepointStmt.lastError().text();
        stats.failed++;
        return false;
    }

    auto rollbackAndFail = [&](const QString &step, const QString &errMsg) {
        stmts.rollbackToStmt.exec();
        stmts.releaseStmt.exec();
        qCWarning(lcLibrary) << "Failed to write file" << res.scanned.path << "at" << step << ":"
                             << errMsg;
        stats.failed++;
    };

    FileOp op = FileOp::Added;
    qint64 fileId = 0;

    if (res.isNew) {
        const auto moveRes = tryResolveMovedRecord(res, stmts, fileId);
        if (moveRes == MoveLookupResult::FoundAndMoved) {
            op = FileOp::Moved;
        } else if (moveRes == MoveLookupResult::Error) {
            rollbackAndFail(
                QStringLiteral("moveRecord"), stmts.updateMovedFileStmt.lastError().text());
            return false;
        } else if (!insertNewRecord(res, stmts, now, fileId)) {
            rollbackAndFail(
                QStringLiteral("insertNewFile"), stmts.insertNewFileStmt.lastError().text());
            return false;
        }
    } else {
        if (!updateExistingRecord(res, stmts, now, fileId)) {
            rollbackAndFail(QStringLiteral("updateFile"), stmts.updateFileStmt.lastError().text());
            return false;
        }
        op = FileOp::Updated;
    }

    qint64 trackId = 0;
    if (!writeTrackRecord(fileId, stmts, now, trackId)) {
        rollbackAndFail(QStringLiteral("writeTrack"), stmts.insertTrackStmt.lastError().text());
        return false;
    }

    if (!writeRawTags(trackId, res.tagResult.value().tags, stmts, now)) {
        rollbackAndFail(QStringLiteral("writeRawTags"), stmts.insertRawTagStmt.lastError().text());
        return false;
    }

    const auto linkRes = entityLinker.linkTrack(trackId);
    if (!linkRes.ok()) {
        rollbackAndFail(QStringLiteral("linkTrack"), linkRes.error().toString());
        return false;
    }

    if (!stmts.releaseStmt.exec()) {
        rollbackAndFail(QStringLiteral("releaseSavepoint"), stmts.releaseStmt.lastError().text());
        return false;
    }

    if (op == FileOp::Moved) {
        stats.moved++;
    } else if (op == FileOp::Added) {
        stats.added++;
    } else if (op == FileOp::Updated) {
        stats.updated++;
    }

    return true;
}

void writeFailedFile(
    const FileReadResult &res, WriterStatements &stmts, ScanStats &stats, qint64 now)
{
    qCDebug(lcLibrary) << "Failed reading tags for" << res.scanned.path << ":"
                       << res.tagResult.error().toString();

    if (!stmts.savepointStmt.exec()) {
        qCWarning(lcLibrary) << "Failed to create savepoint for failed file" << res.scanned.path
                             << ":" << stmts.savepointStmt.lastError().text();
        stats.failed++;
        return;
    }

    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    const QString scanError = res.tagResult.error().toString();

    bool execOk = false;
    if (res.isNew) {
        stmts.insertNewFileFailedStmt.bindValue(0, res.scanned.rootId);
        stmts.insertNewFileFailedStmt.bindValue(1, res.scanned.path);
        stmts.insertNewFileFailedStmt.bindValue(2, res.scanned.size);
        stmts.insertNewFileFailedStmt.bindValue(3, res.scanned.mtimeMs);
        stmts.insertNewFileFailedStmt.bindValue(
            4, contentHash.isEmpty() ? QVariant() : contentHash);
        stmts.insertNewFileFailedStmt.bindValue(5, scanError);
        stmts.insertNewFileFailedStmt.bindValue(6, now);
        stmts.insertNewFileFailedStmt.bindValue(7, now);
        execOk = stmts.insertNewFileFailedStmt.exec();
    } else {
        stmts.updateFileFailedStmt.bindValue(0, res.scanned.rootId);
        stmts.updateFileFailedStmt.bindValue(1, res.scanned.size);
        stmts.updateFileFailedStmt.bindValue(2, res.scanned.mtimeMs);
        stmts.updateFileFailedStmt.bindValue(3, contentHash.isEmpty() ? QVariant() : contentHash);
        stmts.updateFileFailedStmt.bindValue(4, scanError);
        stmts.updateFileFailedStmt.bindValue(5, now);
        stmts.updateFileFailedStmt.bindValue(6, res.existingDbFile.id);
        execOk = stmts.updateFileFailedStmt.exec();
    }

    if (!execOk) {
        stmts.rollbackToStmt.exec();
        stmts.releaseStmt.exec();
        qCWarning(lcLibrary) << "Failed writing failed-file row for" << res.scanned.path;
        stats.failed++;
        return;
    }

    if (!stmts.releaseStmt.exec()) {
        stmts.rollbackToStmt.exec();
        stmts.releaseStmt.exec();
        qCWarning(lcLibrary) << "Failed releasing savepoint for failed file" << res.scanned.path;
        stats.failed++;
        return;
    }

    stats.failed++;
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

core::Result<void> processWriterQueue(const QSqlDatabase &db,
    BoundedQueue<FileReadResult> &resultQueue, int batchSize, int totalToRead,
    const std::function<qint64()> &getNow,
    const std::function<void(const ScanProgress &)> &onProgress, ScanStats &stats)
{
    WriterStatements stmts(db);
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
            writeSuccessfulFile(res, stmts, entityLinker, stats, now);
        } else {
            writeFailedFile(res, stmts, stats, now);
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

} // namespace

core::Result<ScanStats> Scanner::doScan(const QStringList &dirs)
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    ScanStats stats;

    const LibraryRoots libraryRoots(m_db);
    const auto rootsListRes = libraryRoots.list();
    if (!rootsListRes.ok()) {
        stats.error = rootsListRes.error().toString();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return rootsListRes.error();
    }

    QList<LibraryRoot> enabledRoots;
    for (const auto &r : rootsListRes.value()) {
        if (r.enabled) {
            enabledRoots.append(r);
        }
    }

    const auto targets = collectWalkTargets(enabledRoots, dirs);

    QList<ScannedFile> walkedFiles;
    const bool walkOk = walkAllTargets(
        targets, m_cancelled, [this](const ScanProgress &p) { emit progress(p); }, walkedFiles);

    QHash<QString, ScannedFile> walkedMap;
    for (const auto &f : walkedFiles) {
        walkedMap.insert(f.path, f);
    }
    stats.found = static_cast<int>(walkedMap.size());

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        stats.error = connRes.error().toString();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return connRes.error();
    }
    const QSqlDatabase &db = connRes.value();

    auto cleanupOrphans = [&]() {
        performCleanupOrphans(
            db, m_options.coverStore, [this]() { return getNow(); },
            stats.cancelled || m_cancelled.load(), stats);
    };

    if (!walkOk || m_cancelled.load()) {
        stats.cancelled = true;
        cleanupOrphans();
        stats.elapsedMs = totalTimer.elapsed();
        qCInfo(lcLibrary) << "Scan cancelled during walk: found=" << stats.found;
        emit finished(stats);
        return stats;
    }

    const auto dbFilesRes = queryDbFiles(db, enabledRoots, dirs);
    if (!dbFilesRes.ok()) {
        stats.error = dbFilesRes.error().toString();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return dbFilesRes.error();
    }
    const auto &dbFilesByPath = dbFilesRes.value();

    QList<ScannedFile> filesToRead;
    QList<qint64> restoredFileIds;
    QList<qint64> missingFileIds;

    classifyFiles(walkedMap, dbFilesByPath, filesToRead, restoredFileIds, missingFileIds, stats);

    if (m_cancelled.load()) {
        stats.cancelled = true;
        cleanupOrphans();
        stats.elapsedMs = totalTimer.elapsed();
        qCInfo(lcLibrary) << "Scan cancelled before missing update: found=" << stats.found;
        emit finished(stats);
        return stats;
    }

    const auto applyRes = applyMissingAndRestored(db, missingFileIds, restoredFileIds, getNow());
    if (!applyRes.ok()) {
        stats.error = applyRes.error().toString();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return applyRes.error();
    }

    if (filesToRead.isEmpty()) {
        cleanupOrphans();
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
        stats.elapsedMs = totalTimer.elapsed();
        qCInfo(lcLibrary) << "Scan finished: found=" << stats.found << "added=" << stats.added
                          << "updated=" << stats.updated << "unchanged=" << stats.unchanged
                          << "moved=" << stats.moved << "missing=" << stats.missing
                          << "restored=" << stats.restored << "failed=" << stats.failed
                          << "albumsRemoved=" << stats.albumsRemoved
                          << "artistsRemoved=" << stats.artistsRemoved
                          << "cancelled=" << stats.cancelled << "elapsedMs=" << stats.elapsedMs;
        emit finished(stats);
        return stats;
    }

    int maxThreads = m_options.maxThreads > 0 ? m_options.maxThreads : QThread::idealThreadCount();
    maxThreads = std::max(maxThreads, 1);
    const int batchSize = std::max(1, m_options.batchSize);

    const auto pipelineRes = executeReadAndWritePipeline(
        db, filesToRead, dbFilesByPath, batchSize, maxThreads, m_options.coverStore, m_cancelled,
        [this]() { return getNow(); }, [this](const ScanProgress &p) { emit progress(p); }, stats);

    if (!pipelineRes.ok()) {
        stats.error = pipelineRes.error().toString();
        cleanupOrphans();
        stats.elapsedMs = totalTimer.elapsed();
        emit finished(stats);
        return pipelineRes.error();
    }

    stats.cancelled = m_cancelled.load();
    cleanupOrphans();

    emit progress(ScanProgress {
        .phase = ScanProgress::Phase::Finishing,
        .done = static_cast<int>(filesToRead.size()),
        .total = static_cast<int>(filesToRead.size()),
    });

    stats.elapsedMs = totalTimer.elapsed();

    qCInfo(lcLibrary) << "Scan finished: found=" << stats.found << "added=" << stats.added
                      << "updated=" << stats.updated << "unchanged=" << stats.unchanged
                      << "moved=" << stats.moved << "missing=" << stats.missing
                      << "restored=" << stats.restored << "failed=" << stats.failed
                      << "albumsRemoved=" << stats.albumsRemoved
                      << "artistsRemoved=" << stats.artistsRemoved
                      << "cancelled=" << stats.cancelled << "elapsedMs=" << stats.elapsedMs;

    emit finished(stats);
    return stats;
}

} // namespace linernotes::library
