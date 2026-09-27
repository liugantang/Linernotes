// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DatabaseBackup.h"

#include "Database.h"
#include "Errors.h"
#include "LibraryLogging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>
#include <vector>

namespace linernotes::library {

namespace {

const QRegularExpression &backupPattern()
{
    static const QRegularExpression s_pattern(QStringLiteral(R"(^library-(\d{8}-\d{6})\.db$)"));
    return s_pattern;
}

} // namespace

DatabaseBackup::DatabaseBackup(Database &db, Options options)
    : m_db(db)
    , m_options(std::move(options))
{
    m_options.keep = std::clamp(m_options.keep, 1, 60);
}

std::vector<DatabaseBackup::BackupEntry> DatabaseBackup::listBackups() const
{
    if (m_options.backupDir.isEmpty()) {
        return { };
    }

    const QDir dir(m_options.backupDir);
    if (!dir.exists()) {
        return { };
    }

    const QFileInfoList entries
        = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
    std::vector<BackupEntry> backups;
    backups.reserve(entries.size());

    const auto &regex = backupPattern();
    for (const auto &entry : entries) {
        const auto match = regex.match(entry.fileName());
        if (match.hasMatch()) {
            const QString timeStr = match.captured(1);
            const QDateTime dt = QDateTime::fromString(timeStr, QStringLiteral("yyyyMMdd-HHmmss"));
            if (dt.isValid()) {
                backups.push_back(BackupEntry {
                    .filePath = entry.absoluteFilePath(),
                    .fileName = entry.fileName(),
                    .time = dt,
                });
            }
        }
    }

    std::ranges::sort(backups, [](const BackupEntry &a, const BackupEntry &b) {
        if (a.time != b.time) {
            return a.time < b.time;
        }
        return a.fileName < b.fileName;
    });

    return backups;
}

std::optional<QDateTime> DatabaseBackup::latestBackupTime() const
{
    const auto backups = listBackups();
    if (backups.empty()) {
        return std::nullopt;
    }
    return backups.back().time;
}

bool DatabaseBackup::isDue(const QDateTime &now) const
{
    const auto latest = latestBackupTime();
    if (!latest.has_value()) {
        return true;
    }
    QDateTime latestDt = *latest;
    latestDt.setTimeZone(now.timeZone());
    return latestDt.msecsTo(now) >= m_options.intervalMs;
}

core::Result<QString> DatabaseBackup::backupNow(const QDateTime &now)
{
    if (m_options.backupDir.isEmpty()) {
        return core::Error {
            .code = QString(errc::kDbBackup),
            .message = QStringLiteral("Backup directory is empty"),
            .detail = QString(),
        };
    }

    const QDir dir(m_options.backupDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return core::Error {
            .code = QString(errc::kDbBackup),
            .message = QStringLiteral("Failed to create backup directory"),
            .detail = QStringLiteral("Cannot create directory %1").arg(m_options.backupDir),
        };
    }

    const QString timeStr = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString finalFileName = QStringLiteral("library-%1.db").arg(timeStr);
    const QString tmpFileName = QStringLiteral("library-%1.db.tmp").arg(timeStr);
    const QString finalPath = dir.filePath(finalFileName);
    const QString tmpPath = dir.filePath(tmpFileName);

    if (QFile::exists(tmpPath)) {
        QFile::remove(tmpPath);
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &db = connRes.value();

    QSqlQuery q(db);
    q.prepare(QStringLiteral("VACUUM INTO :path"));
    q.bindValue(QStringLiteral(":path"), tmpPath);
    if (!q.exec()) {
        if (QFile::exists(tmpPath)) {
            QFile::remove(tmpPath);
        }
        return core::Error {
            .code = QString(errc::kDbBackup),
            .message = QStringLiteral("Failed to execute VACUUM INTO"),
            .detail = q.lastError().text(),
        };
    }

    if (QFile::exists(finalPath)) {
        QFile::remove(finalPath);
    }

    if (!QFile::rename(tmpPath, finalPath)) {
        if (QFile::exists(tmpPath)) {
            QFile::remove(tmpPath);
        }
        return core::Error {
            .code = QString(errc::kDbBackup),
            .message = QStringLiteral("Failed to rename temporary backup file"),
            .detail = QStringLiteral("From %1 to %2").arg(tmpPath, finalPath),
        };
    }

    // Cleanup old backups exceeding keep
    const auto backups = listBackups();
    if (backups.size() > static_cast<std::size_t>(m_options.keep)) {
        const std::size_t excess = backups.size() - static_cast<std::size_t>(m_options.keep);
        for (std::size_t i = 0; i < excess; ++i) {
            QFile::remove(backups.at(i).filePath);
        }
    }

    return finalPath;
}

} // namespace linernotes::library
