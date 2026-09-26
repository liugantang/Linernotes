// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

/**
 * @file ScanDiff.h
 * @brief Directory walking, DB file querying, and diff classification.
 *
 * 仅供 library 内部使用 (Internal to the library module only).
 */

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <core/Result.h>
#include <library/LibraryRoots.h>
#include <library/Scanner.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>

class QSqlDatabase;

namespace linernotes::library::detail {

struct ScannedFile {
    QString path;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    qint64 rootId = 0;

    bool operator==(const ScannedFile &other) const = default;
};

struct DbFile {
    qint64 id = 0;
    qint64 rootId = 0;
    QString path;
    qint64 size = 0;
    qint64 mtime = 0;
    QString contentHash;
    std::optional<qint64> missingSince;

    bool operator==(const DbFile &other) const = default;
};

struct WalkTarget {
    QString rootPath;
    QString walkDir;
    qint64 rootId = 0;
    QStringList excludes;
};

QList<WalkTarget> collectWalkTargets(
    const QList<LibraryRoot> &enabledRoots, const QStringList &dirs);

bool walkAllTargets(const QList<WalkTarget> &targets, const std::atomic<bool> &cancelled,
    const std::function<void(const ScanProgress &)> &onProgress, QList<ScannedFile> &walkedFiles);

core::Result<QHash<QString, DbFile>> queryDbFiles(
    const QSqlDatabase &db, const QList<LibraryRoot> &enabledRoots, const QStringList &dirs);

void classifyFiles(const QHash<QString, ScannedFile> &walkedMap,
    const QHash<QString, DbFile> &dbFilesByPath, QList<ScannedFile> &filesToRead,
    QList<qint64> &restoredFileIds, QList<qint64> &missingFileIds, ScanStats &stats);

core::Result<void> applyMissingAndRestored(const QSqlDatabase &db,
    const QList<qint64> &missingFileIds, const QList<qint64> &restoredFileIds, qint64 now);

} // namespace linernotes::library::detail
