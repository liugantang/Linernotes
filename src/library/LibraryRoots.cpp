// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LibraryRoots.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

#include <library/Database.h>
#include <library/Errors.h>
#include <library/LibraryLogging.h>

namespace linernotes::library {

namespace {

bool isSubdirectoryOf(const QString &child, const QString &parent)
{
    if (parent == QStringLiteral("/")) {
        return child != QStringLiteral("/");
    }
    return child.startsWith(parent + u'/');
}

} // namespace

LibraryRoots::LibraryRoots(Database &db)
    : m_db(db)
{
}

core::Result<LibraryRoot> LibraryRoots::add(const QString &path, const QStringList &excludes)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) {
        return core::Error {
            .code = QString(errc::kRootInvalid),
            .message = QStringLiteral("Path does not exist or is not a directory"),
            .detail = path,
        };
    }

    QString cleanPath = QDir::cleanPath(fi.absoluteFilePath());
    if (cleanPath.length() > 1 && cleanPath.endsWith(u'/')) {
        cleanPath.chop(1);
    }

    const auto existingRootsRes = list();
    if (!existingRootsRes.ok()) {
        return existingRootsRes.error();
    }

    const auto &existingRoots = existingRootsRes.value();
    for (const auto &root : existingRoots) {
        if (root.path == cleanPath) {
            return root;
        }
        if (isSubdirectoryOf(cleanPath, root.path) || isSubdirectoryOf(root.path, cleanPath)) {
            return core::Error {
                .code = QString(errc::kRootOverlap),
                .message = QStringLiteral("Library root '%1' overlaps with existing root '%2'")
                    .arg(cleanPath, root.path),
                .detail = cleanPath,
            };
        }
    }

    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QJsonArray arr;
    for (const auto &ex : excludes) {
        arr.append(ex);
    }
    const QString excludesJson
        = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    const qint64 addedAt = QDateTime::currentMSecsSinceEpoch();

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral(
        "INSERT INTO library_roots (path, enabled, excludes, added_at) VALUES (?, 1, ?, ?)"));
    q.addBindValue(cleanPath);
    q.addBindValue(excludesJson);
    q.addBindValue(addedAt);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = cleanPath,
        };
    }

    const qint64 id = q.lastInsertId().toLongLong();
    return LibraryRoot {
        .id = id,
        .path = cleanPath,
        .enabled = true,
        .excludes = excludes,
    };
}

core::Result<void> LibraryRoots::remove(qint64 id)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("DELETE FROM library_roots WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(id),
        };
    }

    return { };
}

core::Result<void> LibraryRoots::setEnabled(qint64 id, bool enabled)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("UPDATE library_roots SET enabled = ? WHERE id = ?"));
    q.addBindValue(enabled ? 1 : 0);
    q.addBindValue(id);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(id),
        };
    }

    return { };
}

core::Result<void> LibraryRoots::setExcludes(qint64 id, const QStringList &excludes)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QJsonArray arr;
    for (const auto &ex : excludes) {
        arr.append(ex);
    }
    const QString excludesJson
        = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));

    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("UPDATE library_roots SET excludes = ? WHERE id = ?"));
    q.addBindValue(excludesJson);
    q.addBindValue(id);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = QString::number(id),
        };
    }

    return { };
}

core::Result<QList<LibraryRoot>> LibraryRoots::list() const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery q(connRes.value());
    if (!q.exec(QStringLiteral(
            "SELECT id, path, enabled, excludes FROM library_roots ORDER BY id ASC"))) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }

    QList<LibraryRoot> result;
    while (q.next()) {
        LibraryRoot root;
        root.id = q.value(0).toLongLong();
        root.path = q.value(1).toString();
        root.enabled = (q.value(2).toInt() != 0);

        const QString rawExcludes = q.value(3).toString();
        QJsonParseError parseError { };
        const QJsonDocument doc = QJsonDocument::fromJson(rawExcludes.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            qCWarning(lcLibrary) << "Corrupted excludes JSON in library_roots (id=" << root.id
                                 << "):" << rawExcludes;
            root.excludes = { };
        } else {
            const QJsonArray arr = doc.array();
            for (const auto &val : arr) {
                if (val.isString()) {
                    root.excludes.append(val.toString());
                }
            }
        }
        result.append(std::move(root));
    }

    return result;
}

} // namespace linernotes::library
