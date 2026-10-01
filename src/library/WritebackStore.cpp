// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "WritebackStore.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/Errors.h>
#include <library/TagWriter.h>

namespace linernotes::library {

namespace {

QString extractFieldValue(const QSqlQuery &q, TagField field)
{
    switch (field) {
    case TagField::Title:
        return q.value(0).isNull() ? QString() : q.value(0).toString();
    case TagField::Artist:
        return q.value(1).isNull() ? QString() : q.value(1).toString();
    case TagField::Album:
        return q.value(2).isNull() ? QString() : q.value(2).toString();
    case TagField::AlbumArtist:
        return q.value(3).isNull() ? QString() : q.value(3).toString();
    case TagField::Genre:
        return q.value(4).isNull() ? QString() : q.value(4).toString();
    case TagField::Composer:
        return q.value(5).isNull() ? QString() : q.value(5).toString();
    case TagField::Year:
        return q.value(6).isNull() ? QString() : QString::number(q.value(6).toInt());
    case TagField::TrackNumber:
        return q.value(7).isNull() ? QString() : QString::number(q.value(7).toInt());
    case TagField::TrackTotal:
        return q.value(8).isNull() ? QString() : QString::number(q.value(8).toInt());
    case TagField::DiscNumber:
        return q.value(9).isNull() ? QString() : QString::number(q.value(9).toInt());
    case TagField::DiscTotal:
        return q.value(10).isNull() ? QString() : QString::number(q.value(10).toInt());
    }
    return { };
}

QJsonObject fieldsToJson(const QHash<TagField, QString> &fields)
{
    QJsonObject obj;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        obj.insert(tagFieldToColumn(it.key()), it.value());
    }
    return obj;
}

QHash<TagField, QString> fieldsFromJson(const QJsonObject &obj)
{
    QHash<TagField, QString> fields;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (const auto field = tagFieldFromColumn(it.key()); field.has_value()) {
            fields.insert(field.value(), it.value().toString());
        }
    }
    return fields;
}

struct FileGroup {
    QString path;
    bool isCue = false;
    qint64 trackId = 0;
    QSet<TagField> fields;
};

} // namespace

WritebackStore::WritebackStore(Database &db)
    : m_db(db)
{
}

core::Result<WritebackPlan> WritebackStore::plan(qint64 batchId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("SELECT c.entity_id, c.field, t.file_id, t.cue_index, f.path "
                       "FROM corrections c "
                       "JOIN tracks t ON t.id = c.entity_id "
                       "JOIN files f ON f.id = t.file_id "
                       "WHERE c.batch_id = ? AND c.status = 'accepted' AND c.entity_type = 'track' "
                       "ORDER BY f.id, c.id;"));
    q.addBindValue(batchId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to query accepted corrections for writeback plan"),
            .detail = q.lastError().text(),
        };
    }

    QList<qint64> fileOrder;
    QHash<qint64, FileGroup> groups;
    while (q.next()) {
        const qint64 trackId = q.value(0).toLongLong();
        const QString fieldStr = q.value(1).toString();
        const qint64 fileId = q.value(2).toLongLong();
        const bool cue = !q.value(3).isNull();
        const QString path = q.value(4).toString();

        if (!groups.contains(fileId)) {
            fileOrder.append(fileId);
            groups.insert(fileId,
                FileGroup {
                    .path = path,
                    .isCue = cue,
                    .trackId = trackId,
                    .fields = { },
                });
        }
        auto &grp = groups.find(fileId).value();
        if (cue) {
            grp.isCue = true;
        }
        if (const auto fieldOpt = tagFieldFromColumn(fieldStr); fieldOpt.has_value()) {
            grp.fields.insert(fieldOpt.value());
        }
    }

    WritebackPlan plan;
    for (const qint64 fileId : fileOrder) {
        const auto &grp = groups.value(fileId);
        if (grp.isCue) {
            plan.skippedCue++;
            plan.skippedFiles.append(PlannedFile {
                .fileId = fileId,
                .path = grp.path,
                .fields = { },
                .status = WritebackFileStatus::Skipped,
                .error = QStringLiteral("CUE track"),
            });
            continue;
        }
        if (!TagWriter::isSupported(grp.path)) {
            plan.skippedUnsupported++;
            plan.skippedFiles.append(PlannedFile {
                .fileId = fileId,
                .path = grp.path,
                .fields = { },
                .status = WritebackFileStatus::Skipped,
                .error = QStringLiteral("Unsupported format"),
            });
            continue;
        }

        QSqlQuery effQ(conn);
        effQ.prepare(QStringLiteral("SELECT title, artist, album, album_artist, genre, composer, "
                                    "year, track_number, track_total, disc_number, disc_total "
                                    "FROM effective_metadata WHERE track_id = ?;"));
        effQ.addBindValue(grp.trackId);

        QHash<TagField, QString> fields;
        if (effQ.exec() && effQ.next()) {
            for (const auto field : grp.fields) {
                fields.insert(field, extractFieldValue(effQ, field));
            }
        } else {
            for (const auto field : grp.fields) {
                fields.insert(field, QString());
            }
        }

        plan.files.append(PlannedFile {
            .fileId = fileId,
            .path = grp.path,
            .fields = fields,
            .status = WritebackFileStatus::Pending,
            .error = QString(),
        });
    }

    return plan;
}

core::Result<qint64> WritebackStore::create(qint64 batchId, const WritebackPlan &plan, qint64 now)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    Transaction tx(conn);

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT INTO writebacks (batch_id, created_at, reverted_at) VALUES (?, ?, NULL);"));
    q.addBindValue(batchId);
    q.addBindValue(now);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to insert writeback record"),
            .detail = q.lastError().text(),
        };
    }
    const qint64 wbId = q.lastInsertId().toLongLong();

    for (const auto &pf : plan.files) {
        const QString fieldsJson = QString::fromUtf8(
            QJsonDocument(fieldsToJson(pf.fields)).toJson(QJsonDocument::Compact));
        QSqlQuery fq(conn);
        fq.prepare(QStringLiteral(
            "INSERT INTO writeback_files (writeback_id, file_id, path, fields, snapshot, status, "
            "error, written_size, written_mtime) "
            "VALUES (?, ?, ?, ?, NULL, 'pending', NULL, NULL, NULL);"));
        fq.addBindValue(wbId);
        fq.addBindValue(pf.fileId);
        fq.addBindValue(pf.path);
        fq.addBindValue(fieldsJson);
        if (!fq.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = QStringLiteral("Failed to insert writeback file record"),
                .detail = fq.lastError().text(),
            };
        }
    }

    for (const auto &pf : plan.skippedFiles) {
        const QString fieldsJson = QString::fromUtf8(
            QJsonDocument(fieldsToJson(pf.fields)).toJson(QJsonDocument::Compact));
        QSqlQuery fq(conn);
        fq.prepare(QStringLiteral(
            "INSERT INTO writeback_files (writeback_id, file_id, path, fields, snapshot, status, "
            "error, written_size, written_mtime) "
            "VALUES (?, ?, ?, ?, NULL, 'skipped', ?, NULL, NULL);"));
        fq.addBindValue(wbId);
        fq.addBindValue(pf.fileId);
        fq.addBindValue(pf.path);
        fq.addBindValue(fieldsJson);
        fq.addBindValue(pf.error);
        if (!fq.exec()) {
            return core::Error {
                .code = QString(errc::kDbQuery),
                .message = QStringLiteral("Failed to insert skipped writeback file record"),
                .detail = fq.lastError().text(),
            };
        }
    }

    const auto commitRes = tx.commit();
    if (!commitRes.ok()) {
        return commitRes.error();
    }
    return wbId;
}

std::optional<qint64> WritebackStore::activeWriteback(qint64 batchId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return std::nullopt;
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT id FROM writebacks WHERE batch_id = ? AND reverted_at IS NULL "
                             "ORDER BY created_at DESC, id DESC LIMIT 1;"));
    q.addBindValue(batchId);
    if (q.exec() && q.next()) {
        return q.value(0).toLongLong();
    }
    return std::nullopt;
}

core::Result<void> WritebackStore::saveSnapshot(
    qint64 wbId, qint64 fileId, const QString &snapshotJson)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "UPDATE writeback_files SET snapshot = ? WHERE writeback_id = ? AND file_id = ?;"));
    q.addBindValue(snapshotJson);
    q.addBindValue(wbId);
    q.addBindValue(fileId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to save snapshot"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> WritebackStore::markWritten(
    qint64 wbId, qint64 fileId, qint64 size, qint64 mtime)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "UPDATE writeback_files SET status = 'written', written_size = ?, written_mtime = ?, "
        "error = NULL WHERE writeback_id = ? AND file_id = ?;"));
    q.addBindValue(size);
    q.addBindValue(mtime);
    q.addBindValue(wbId);
    q.addBindValue(fileId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to mark file as written"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> WritebackStore::markFailed(
    qint64 wbId, qint64 fileId, WritebackFileStatus status, const QString &error)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("UPDATE writeback_files SET status = ?, error = ? WHERE writeback_id "
                             "= ? AND file_id = ?;"));
    q.addBindValue(writebackFileStatusToString(status));
    q.addBindValue(error);
    q.addBindValue(wbId);
    q.addBindValue(fileId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to mark file as failed"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> WritebackStore::markReverted(qint64 wbId, qint64 fileId)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("UPDATE writeback_files SET status = 'reverted', error = NULL "
                             "WHERE writeback_id = ? AND file_id = ?;"));
    q.addBindValue(wbId);
    q.addBindValue(fileId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to mark file as reverted"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<void> WritebackStore::finishRevert(qint64 wbId, qint64 now)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("UPDATE writebacks SET reverted_at = ? WHERE id = ?;"));
    q.addBindValue(now);
    q.addBindValue(wbId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to finish writeback revert"),
            .detail = q.lastError().text(),
        };
    }
    return { };
}

core::Result<WritebackCounts> WritebackStore::counts(qint64 wbId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT status, COUNT(*) FROM writeback_files WHERE writeback_id = ? GROUP BY status;"));
    q.addBindValue(wbId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to query writeback counts"),
            .detail = q.lastError().text(),
        };
    }

    WritebackCounts c;
    while (q.next()) {
        const QString statusStr = q.value(0).toString();
        const int count = q.value(1).toInt();
        c.total += count;
        if (statusStr == QStringLiteral("pending")) {
            c.pending += count;
        } else if (statusStr == QStringLiteral("written")) {
            c.written += count;
        } else if (statusStr == QStringLiteral("failed")) {
            c.failed += count;
        } else if (statusStr == QStringLiteral("reverted")) {
            c.reverted += count;
        } else if (statusStr == QStringLiteral("revert_failed")) {
            c.revertFailed += count;
        } else if (statusStr == QStringLiteral("skipped")) {
            c.skipped += count;
        }
    }
    return c;
}

core::Result<WritebackFileRecord> WritebackStore::fileRecord(qint64 wbId, qint64 fileId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("SELECT path, fields, snapshot, status, error, written_size, written_mtime "
                       "FROM writeback_files WHERE writeback_id = ? AND file_id = ?;"));
    q.addBindValue(wbId);
    q.addBindValue(fileId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to query writeback file record"),
            .detail = q.lastError().text(),
        };
    }
    if (!q.next()) {
        return core::Error {
            .code = QString(errc::kWritebackNotFound),
            .message = QStringLiteral("Writeback file record not found"),
            .detail = QStringLiteral("wbId=%1, fileId=%2").arg(wbId).arg(fileId),
        };
    }

    const QString path = q.value(0).toString();
    const QString fieldsJson = q.value(1).toString();
    const QString snapshot = q.value(2).isNull() ? QString() : q.value(2).toString();
    const QString statusStr = q.value(3).toString();
    const QString error = q.value(4).isNull() ? QString() : q.value(4).toString();
    const qint64 writtenSize = q.value(5).isNull() ? 0 : q.value(5).toLongLong();
    const qint64 writtenMtime = q.value(6).isNull() ? 0 : q.value(6).toLongLong();

    const auto doc = QJsonDocument::fromJson(fieldsJson.toUtf8());
    const auto fields = fieldsFromJson(doc.object());
    const auto status
        = writebackFileStatusFromString(statusStr).value_or(WritebackFileStatus::Pending);

    return WritebackFileRecord {
        .writebackId = wbId,
        .fileId = fileId,
        .path = path,
        .fields = fields,
        .snapshot = snapshot,
        .status = status,
        .error = error,
        .writtenSize = writtenSize,
        .writtenMtime = writtenMtime,
    };
}

core::Result<QList<qint64>> WritebackStore::writtenFileIds(qint64 wbId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT file_id FROM writeback_files WHERE writeback_id = ? AND status = 'written' "
        "ORDER BY file_id;"));
    q.addBindValue(wbId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to query written file IDs for writeback"),
            .detail = q.lastError().text(),
        };
    }

    QList<qint64> ids;
    while (q.next()) {
        ids.append(q.value(0).toLongLong());
    }
    return ids;
}

core::Result<QStringList> WritebackStore::affectedDirectories(qint64 wbId) const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT DISTINCT path FROM writeback_files WHERE writeback_id = ?;"));
    q.addBindValue(wbId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(errc::kDbQuery),
            .message = QStringLiteral("Failed to query affected directories"),
            .detail = q.lastError().text(),
        };
    }

    QSet<QString> dirs;
    while (q.next()) {
        const QString path = q.value(0).toString();
        const QString dir = QFileInfo(path).path();
        if (!dir.isEmpty()) {
            dirs.insert(dir);
        }
    }
    return dirs.values();
}

} // namespace linernotes::library
