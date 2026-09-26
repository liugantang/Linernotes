// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

/**
 * @file ScanWriter.h
 * @brief Scan results database writer.
 *
 * 仅供 library 内部使用 (Internal to the library module only).
 */

#include <QList>
#include <QSqlQuery>
#include <QString>

#include <core/Result.h>
#include <library/CoverStore.h>
#include <library/ScanDiff.h>
#include <library/Scanner.h>
#include <library/TagReader.h>

#include <cstdint>
#include <optional>

class QSqlDatabase;

namespace linernotes::library {
class EntityLinker;
}

namespace linernotes::library::detail {

struct CoverInfo {
    enum class Source : std::uint8_t { None, Embedded, Folder };
    Source source = Source::None;
    QString sourcePath;
    CoverStore::Info info;
};

struct FileReadResult {
    ScannedFile scanned;
    bool isNew = true;
    DbFile existingDbFile;
    core::Result<TagReadResult> tagResult = core::Error { };
    core::Result<QString> fingerprintResult = core::Error { };
    CoverInfo cover;
};

class ScanWriter {
public:
    explicit ScanWriter(const QSqlDatabase &db);

    bool writeSuccessfulFile(
        const FileReadResult &res, EntityLinker &entityLinker, ScanStats &stats, qint64 now);
    void writeFailedFile(const FileReadResult &res, ScanStats &stats, qint64 now);

private:
    enum class FileOp : std::uint8_t { Added, Updated, Moved };
    enum class MoveLookupResult : std::uint8_t { FoundAndMoved, NotFound, Error };

    MoveLookupResult tryResolveMovedRecord(const FileReadResult &res, qint64 &outFileId);
    std::optional<qint64> resolveCoverId(const CoverInfo &cover, qint64 now);
    bool insertNewRecord(const FileReadResult &res, qint64 now, qint64 &outFileId);
    bool updateExistingRecord(const FileReadResult &res, qint64 now, qint64 &outFileId);
    bool writeTrackRecord(qint64 fileId, qint64 now, qint64 &outTrackId);
    bool writeRawTags(qint64 trackId, const QList<RawTag> &tags, qint64 now);

    QSqlQuery m_savepointStmt;
    QSqlQuery m_releaseStmt;
    QSqlQuery m_rollbackToStmt;
    QSqlQuery m_findMissingStmt;
    QSqlQuery m_updateMovedFileStmt;
    QSqlQuery m_insertNewFileStmt;
    QSqlQuery m_updateFileStmt;
    QSqlQuery m_insertNewFileFailedStmt;
    QSqlQuery m_updateFileFailedStmt;
    QSqlQuery m_findTrackStmt;
    QSqlQuery m_insertTrackStmt;
    QSqlQuery m_deleteRawTagsStmt;
    QSqlQuery m_insertRawTagStmt;
    QSqlQuery m_updateTrackTagsReadAtStmt;
    QSqlQuery m_findCoverStmt;
    QSqlQuery m_insertCoverStmt;
};

} // namespace linernotes::library::detail
