// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ScanDiff.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <library/Database.h>
#include <library/DirectoryWalker.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>

namespace linernotes::library::detail {

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

} // namespace linernotes::library::detail
