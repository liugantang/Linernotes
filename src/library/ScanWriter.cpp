// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ScanWriter.h"

#include <QByteArray>
#include <QMetaType>
#include <QSqlDatabase>
#include <QSqlError>
#include <QVariant>

#include <library/EntityLinker.h>
#include <library/LibraryLogging.h>

namespace linernotes::library::detail {

ScanWriter::ScanWriter(const QSqlDatabase &db)
    : m_savepointStmt(db)
    , m_releaseStmt(db)
    , m_rollbackToStmt(db)
    , m_findMissingStmt(db)
    , m_updateMovedFileStmt(db)
    , m_insertNewFileStmt(db)
    , m_updateFileStmt(db)
    , m_insertNewFileFailedStmt(db)
    , m_updateFileFailedStmt(db)
    , m_findTrackStmt(db)
    , m_insertTrackStmt(db)
    , m_deleteRawTagsStmt(db)
    , m_insertRawTagStmt(db)
    , m_updateTrackTagsReadAtStmt(db)
    , m_findCoverStmt(db)
    , m_insertCoverStmt(db)
{
    m_savepointStmt.prepare(QStringLiteral("SAVEPOINT scan_file;"));
    m_releaseStmt.prepare(QStringLiteral("RELEASE scan_file;"));
    m_rollbackToStmt.prepare(QStringLiteral("ROLLBACK TO scan_file;"));

    m_findMissingStmt.prepare(QStringLiteral(
        "SELECT id, root_id, path FROM files WHERE missing_since IS NOT NULL AND size = ? "
        "AND content_hash = ? ORDER BY missing_since ASC LIMIT 1"));
    m_updateMovedFileStmt.prepare(QStringLiteral(
        "UPDATE files SET path = ?, root_id = ?, mtime = ?, missing_since = NULL WHERE id = "
        "?"));
    m_insertNewFileStmt.prepare(QStringLiteral(
        "INSERT INTO files (root_id, path, size, mtime, content_hash, container, codec, "
        "duration_ms, bitrate, sample_rate, bit_depth, channels, has_embedded_cover, "
        "cover_id, scan_error, first_seen_at, scanned_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL, ?, ?)"));
    m_updateFileStmt.prepare(QStringLiteral(
        "UPDATE files SET root_id = ?, size = ?, mtime = ?, content_hash = ?, container = ?, "
        "codec = ?, duration_ms = ?, bitrate = ?, sample_rate = ?, bit_depth = ?, channels "
        "= ?, has_embedded_cover = ?, cover_id = ?, scan_error = NULL, scanned_at = ?, "
        "missing_since = NULL "
        "WHERE id = ?"));
    m_insertNewFileFailedStmt.prepare(
        QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, scan_error, "
                       "first_seen_at, scanned_at) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    m_updateFileFailedStmt.prepare(QStringLiteral(
        "UPDATE files SET root_id = ?, size = ?, mtime = ?, content_hash = ?, scan_error = "
        "?, scanned_at = ?, missing_since = NULL WHERE id = ?"));
    m_findTrackStmt.prepare(
        QStringLiteral("SELECT id FROM tracks WHERE file_id = ? AND cue_index IS NULL LIMIT 1"));
    m_insertTrackStmt.prepare(
        QStringLiteral("INSERT INTO tracks (file_id, cue_index, tags_read_at, created_at) "
                       "VALUES (?, NULL, ?, ?)"));
    m_deleteRawTagsStmt.prepare(QStringLiteral("DELETE FROM raw_tags WHERE track_id = ?"));
    m_insertRawTagStmt.prepare(QStringLiteral(
        "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value, raw_bytes, "
        "raw_encoding) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    m_updateTrackTagsReadAtStmt.prepare(
        QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?"));
    m_findCoverStmt.prepare(QStringLiteral("SELECT id FROM covers WHERE hash = ? LIMIT 1"));
    m_insertCoverStmt.prepare(QStringLiteral(
        "INSERT INTO covers (hash, mime, width, height, source, source_path, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) ON CONFLICT(hash) DO NOTHING"));
}

ScanWriter::MoveLookupResult ScanWriter::tryResolveMovedRecord(
    const FileReadResult &res, qint64 &outFileId)
{
    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    if (contentHash.isEmpty()) {
        return MoveLookupResult::NotFound;
    }

    m_findMissingStmt.bindValue(0, res.scanned.size);
    m_findMissingStmt.bindValue(1, contentHash);
    if (!m_findMissingStmt.exec()) {
        return MoveLookupResult::Error;
    }
    if (!m_findMissingStmt.next()) {
        return MoveLookupResult::NotFound;
    }

    const qint64 missingId = m_findMissingStmt.value(0).toLongLong();
    m_updateMovedFileStmt.bindValue(0, res.scanned.path);
    m_updateMovedFileStmt.bindValue(1, res.scanned.rootId);
    m_updateMovedFileStmt.bindValue(2, res.scanned.mtimeMs);
    m_updateMovedFileStmt.bindValue(3, missingId);
    if (!m_updateMovedFileStmt.exec()) {
        return MoveLookupResult::Error;
    }
    outFileId = missingId;
    return MoveLookupResult::FoundAndMoved;
}

std::optional<qint64> ScanWriter::resolveCoverId(const CoverInfo &cover, qint64 now)
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

    m_insertCoverStmt.bindValue(0, cover.info.hash);
    m_insertCoverStmt.bindValue(1, cover.info.mime);
    m_insertCoverStmt.bindValue(2, cover.info.width > 0 ? QVariant(cover.info.width) : QVariant());
    m_insertCoverStmt.bindValue(
        3, cover.info.height > 0 ? QVariant(cover.info.height) : QVariant());
    m_insertCoverStmt.bindValue(4, sourceStr);
    m_insertCoverStmt.bindValue(5, sourcePathVal);
    m_insertCoverStmt.bindValue(6, now);
    if (!m_insertCoverStmt.exec()) {
        qCWarning(lcLibrary) << "Failed to insert cover:" << m_insertCoverStmt.lastError().text();
        return std::nullopt;
    }

    m_findCoverStmt.bindValue(0, cover.info.hash);
    if (!m_findCoverStmt.exec() || !m_findCoverStmt.next()) {
        qCWarning(lcLibrary) << "Failed to find cover id after insert:"
                             << m_findCoverStmt.lastError().text();
        return std::nullopt;
    }

    return m_findCoverStmt.value(0).toLongLong();
}

bool ScanWriter::insertNewRecord(const FileReadResult &res, qint64 now, qint64 &outFileId)
{
    const auto &tagRes = res.tagResult.value();
    const auto &audio = tagRes.audio;
    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    const std::optional<qint64> coverId = resolveCoverId(res.cover, now);

    m_insertNewFileStmt.bindValue(0, res.scanned.rootId);
    m_insertNewFileStmt.bindValue(1, res.scanned.path);
    m_insertNewFileStmt.bindValue(2, res.scanned.size);
    m_insertNewFileStmt.bindValue(3, res.scanned.mtimeMs);
    m_insertNewFileStmt.bindValue(4, contentHash.isEmpty() ? QVariant() : contentHash);
    m_insertNewFileStmt.bindValue(5, audio.container);
    m_insertNewFileStmt.bindValue(6, audio.codec);
    m_insertNewFileStmt.bindValue(7, audio.durationMs);
    m_insertNewFileStmt.bindValue(8, audio.bitrate);
    m_insertNewFileStmt.bindValue(9, audio.sampleRate);
    m_insertNewFileStmt.bindValue(10, audio.bitDepth);
    m_insertNewFileStmt.bindValue(11, audio.channels);
    m_insertNewFileStmt.bindValue(12, tagRes.hasEmbeddedCover ? 1 : 0);
    m_insertNewFileStmt.bindValue(13, coverId.has_value() ? QVariant(coverId.value()) : QVariant());
    m_insertNewFileStmt.bindValue(14, now);
    m_insertNewFileStmt.bindValue(15, now);
    if (!m_insertNewFileStmt.exec()) {
        return false;
    }
    outFileId = m_insertNewFileStmt.lastInsertId().toLongLong();
    return true;
}

bool ScanWriter::updateExistingRecord(const FileReadResult &res, qint64 now, qint64 &outFileId)
{
    const auto &tagRes = res.tagResult.value();
    const auto &audio = tagRes.audio;
    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    const std::optional<qint64> coverId = resolveCoverId(res.cover, now);
    outFileId = res.existingDbFile.id;

    m_updateFileStmt.bindValue(0, res.scanned.rootId);
    m_updateFileStmt.bindValue(1, res.scanned.size);
    m_updateFileStmt.bindValue(2, res.scanned.mtimeMs);
    m_updateFileStmt.bindValue(3, contentHash.isEmpty() ? QVariant() : contentHash);
    m_updateFileStmt.bindValue(4, audio.container);
    m_updateFileStmt.bindValue(5, audio.codec);
    m_updateFileStmt.bindValue(6, audio.durationMs);
    m_updateFileStmt.bindValue(7, audio.bitrate);
    m_updateFileStmt.bindValue(8, audio.sampleRate);
    m_updateFileStmt.bindValue(9, audio.bitDepth);
    m_updateFileStmt.bindValue(10, audio.channels);
    m_updateFileStmt.bindValue(11, tagRes.hasEmbeddedCover ? 1 : 0);
    m_updateFileStmt.bindValue(12, coverId.has_value() ? QVariant(coverId.value()) : QVariant());
    m_updateFileStmt.bindValue(13, now);
    m_updateFileStmt.bindValue(14, outFileId);
    return m_updateFileStmt.exec();
}

bool ScanWriter::writeTrackRecord(qint64 fileId, qint64 now, qint64 &outTrackId)
{
    m_findTrackStmt.bindValue(0, fileId);
    if (!m_findTrackStmt.exec()) {
        return false;
    }
    if (m_findTrackStmt.next()) {
        outTrackId = m_findTrackStmt.value(0).toLongLong();
        return true;
    }

    m_insertTrackStmt.bindValue(0, fileId);
    m_insertTrackStmt.bindValue(1, now);
    m_insertTrackStmt.bindValue(2, now);
    if (!m_insertTrackStmt.exec()) {
        return false;
    }
    outTrackId = m_insertTrackStmt.lastInsertId().toLongLong();
    return true;
}

bool ScanWriter::writeRawTags(qint64 trackId, const QList<RawTag> &tags, qint64 now)
{
    m_deleteRawTagsStmt.bindValue(0, trackId);
    if (!m_deleteRawTagsStmt.exec()) {
        return false;
    }

    for (const auto &tag : tags) {
        m_insertRawTagStmt.bindValue(0, trackId);
        m_insertRawTagStmt.bindValue(1, tag.tagType);
        m_insertRawTagStmt.bindValue(2, tag.priority);
        m_insertRawTagStmt.bindValue(3, tag.key);
        m_insertRawTagStmt.bindValue(4, tag.ordinal);
        m_insertRawTagStmt.bindValue(5, tag.value);
        if (tag.rawBytes.isEmpty()) {
            m_insertRawTagStmt.bindValue(6, QVariant(QMetaType::fromType<QByteArray>()));
        } else {
            m_insertRawTagStmt.bindValue(6, tag.rawBytes);
        }
        if (tag.rawEncoding.isEmpty()) {
            m_insertRawTagStmt.bindValue(7, QVariant(QMetaType::fromType<QString>()));
        } else {
            m_insertRawTagStmt.bindValue(7, tag.rawEncoding);
        }
        if (!m_insertRawTagStmt.exec()) {
            return false;
        }
    }

    m_updateTrackTagsReadAtStmt.bindValue(0, now);
    m_updateTrackTagsReadAtStmt.bindValue(1, trackId);
    return m_updateTrackTagsReadAtStmt.exec();
}

bool ScanWriter::writeSuccessfulFile(
    const FileReadResult &res, EntityLinker &entityLinker, ScanStats &stats, qint64 now)
{
    if (!m_savepointStmt.exec()) {
        qCWarning(lcLibrary) << "Failed to create savepoint for" << res.scanned.path << ":"
                             << m_savepointStmt.lastError().text();
        stats.failed++;
        return false;
    }

    auto rollbackAndFail = [&](const QString &step, const QString &errMsg) {
        m_rollbackToStmt.exec();
        m_releaseStmt.exec();
        qCWarning(lcLibrary) << "Failed to write file" << res.scanned.path << "at" << step << ":"
                             << errMsg;
        stats.failed++;
    };

    FileOp op = FileOp::Added;
    qint64 fileId = 0;

    if (res.isNew) {
        const auto moveRes = tryResolveMovedRecord(res, fileId);
        if (moveRes == MoveLookupResult::FoundAndMoved) {
            op = FileOp::Moved;
        } else if (moveRes == MoveLookupResult::Error) {
            rollbackAndFail(QStringLiteral("moveRecord"), m_updateMovedFileStmt.lastError().text());
            return false;
        } else if (!insertNewRecord(res, now, fileId)) {
            rollbackAndFail(
                QStringLiteral("insertNewFile"), m_insertNewFileStmt.lastError().text());
            return false;
        }
    } else {
        if (!updateExistingRecord(res, now, fileId)) {
            rollbackAndFail(QStringLiteral("updateFile"), m_updateFileStmt.lastError().text());
            return false;
        }
        op = FileOp::Updated;
    }

    qint64 trackId = 0;
    if (!writeTrackRecord(fileId, now, trackId)) {
        rollbackAndFail(QStringLiteral("writeTrack"), m_insertTrackStmt.lastError().text());
        return false;
    }

    if (!writeRawTags(trackId, res.tagResult.value().tags, now)) {
        rollbackAndFail(QStringLiteral("writeRawTags"), m_insertRawTagStmt.lastError().text());
        return false;
    }

    const auto linkRes = entityLinker.linkTrack(trackId);
    if (!linkRes.ok()) {
        rollbackAndFail(QStringLiteral("linkTrack"), linkRes.error().toString());
        return false;
    }

    if (!m_releaseStmt.exec()) {
        rollbackAndFail(QStringLiteral("releaseSavepoint"), m_releaseStmt.lastError().text());
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

void ScanWriter::writeFailedFile(const FileReadResult &res, ScanStats &stats, qint64 now)
{
    qCDebug(lcLibrary) << "Failed reading tags for" << res.scanned.path << ":"
                       << res.tagResult.error().toString();

    if (!m_savepointStmt.exec()) {
        qCWarning(lcLibrary) << "Failed to create savepoint for failed file" << res.scanned.path
                             << ":" << m_savepointStmt.lastError().text();
        stats.failed++;
        return;
    }

    const QString contentHash
        = res.fingerprintResult.ok() ? res.fingerprintResult.value() : QString();
    const QString scanError = res.tagResult.error().toString();

    bool execOk = false;
    if (res.isNew) {
        m_insertNewFileFailedStmt.bindValue(0, res.scanned.rootId);
        m_insertNewFileFailedStmt.bindValue(1, res.scanned.path);
        m_insertNewFileFailedStmt.bindValue(2, res.scanned.size);
        m_insertNewFileFailedStmt.bindValue(3, res.scanned.mtimeMs);
        m_insertNewFileFailedStmt.bindValue(4, contentHash.isEmpty() ? QVariant() : contentHash);
        m_insertNewFileFailedStmt.bindValue(5, scanError);
        m_insertNewFileFailedStmt.bindValue(6, now);
        m_insertNewFileFailedStmt.bindValue(7, now);
        execOk = m_insertNewFileFailedStmt.exec();
    } else {
        m_updateFileFailedStmt.bindValue(0, res.scanned.rootId);
        m_updateFileFailedStmt.bindValue(1, res.scanned.size);
        m_updateFileFailedStmt.bindValue(2, res.scanned.mtimeMs);
        m_updateFileFailedStmt.bindValue(3, contentHash.isEmpty() ? QVariant() : contentHash);
        m_updateFileFailedStmt.bindValue(4, scanError);
        m_updateFileFailedStmt.bindValue(5, now);
        m_updateFileFailedStmt.bindValue(6, res.existingDbFile.id);
        execOk = m_updateFileFailedStmt.exec();
    }

    if (!execOk) {
        m_rollbackToStmt.exec();
        m_releaseStmt.exec();
        qCWarning(lcLibrary) << "Failed writing failed-file row for" << res.scanned.path;
        stats.failed++;
        return;
    }

    if (!m_releaseStmt.exec()) {
        m_rollbackToStmt.exec();
        m_releaseStmt.exec();
        qCWarning(lcLibrary) << "Failed releasing savepoint for failed file" << res.scanned.path;
        stats.failed++;
        return;
    }

    stats.failed++;
}

} // namespace linernotes::library::detail
