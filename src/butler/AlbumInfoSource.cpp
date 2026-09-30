// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AlbumInfoSource.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <butler/Errors.h>
#include <library/Database.h>
#include <library/Errors.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

struct RawTrackData {
    AlbumInfoTrack track;
    QString filePath;
};

std::optional<int> readPositiveInt(const QVariant &val)
{
    if (!val.isNull() && val.toInt() > 0) {
        return val.toInt();
    }
    return std::nullopt;
}

RawTrackData readTrackRow(const QSqlQuery &q)
{
    RawTrackData data;
    data.track.trackId = q.value(0).toLongLong();
    data.filePath = q.value(1).toString();
    data.track.durationMs = q.value(2).isNull() ? 0 : q.value(2).toLongLong();
    data.track.title = q.value(3).toString();
    data.track.titleUnusable = q.value(4).toBool();
    data.track.artist = q.value(5).toString();
    data.track.albumArtist = q.value(6).toString();
    data.track.year = readPositiveInt(q.value(7));
    data.track.trackNumber = readPositiveInt(q.value(8));
    data.track.discNumber = readPositiveInt(q.value(9));
    data.track.trackTotal = readPositiveInt(q.value(10));
    data.track.discTotal = readPositiveInt(q.value(11));
    return data;
}

QString longestCommonParentDirectory(const QStringList &dirPaths)
{
    if (dirPaths.isEmpty()) {
        return { };
    }
    if (dirPaths.size() == 1) {
        return QDir::cleanPath(dirPaths.first());
    }

    QStringList commonSegments = QDir::cleanPath(dirPaths.first()).split(QLatin1Char('/'));

    for (qsizetype i = 1; i < dirPaths.size(); ++i) {
        const QStringList segments = QDir::cleanPath(dirPaths.at(i)).split(QLatin1Char('/'));
        const qsizetype maxLen = std::min(commonSegments.size(), segments.size());
        qsizetype commonLen = 0;
        while (commonLen < maxLen && commonSegments.at(commonLen) == segments.at(commonLen)) {
            ++commonLen;
        }
        while (commonSegments.size() > commonLen) {
            commonSegments.removeLast();
        }
        if (commonSegments.isEmpty()) {
            break;
        }
    }

    if (commonSegments.isEmpty()
        || (commonSegments.size() == 1 && commonSegments.first().isEmpty())) {
        return QStringLiteral("/");
    }
    return commonSegments.join(QLatin1Char('/'));
}

bool compareAlbumInfoTracks(const AlbumInfoTrack &a, const AlbumInfoTrack &b)
{
    const int discA = a.discNumber.value_or(0);
    const int discB = b.discNumber.value_or(0);
    if (discA != discB) {
        return discA < discB;
    }
    const int trkA = a.trackNumber.value_or(0);
    const int trkB = b.trackNumber.value_or(0);
    if (trkA != trkB) {
        return trkA < trkB;
    }
    const int cmp = QString::compare(a.relativePath, b.relativePath, Qt::CaseSensitive);
    if (cmp != 0) {
        return cmp < 0;
    }
    return a.trackId < b.trackId;
}

core::Result<QPair<QString, QString>> queryAlbumMetadata(const QSqlDatabase &conn, qint64 albumId)
{
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT title, album_artist FROM albums WHERE id = ?;"));
    q.addBindValue(albumId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }
    if (!q.next()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = QStringLiteral("Album not found"),
            .detail = QString::number(albumId),
        };
    }
    return qMakePair(q.value(0).toString(), q.value(1).toString());
}

core::Result<QList<AlbumInfoTrack>> queryTracksForAlbum(
    const QSqlDatabase &conn, qint64 albumId, QString &outDirectory)
{
    QSqlQuery q(conn);
    q.prepare(
        QStringLiteral("SELECT t.id, f.path, f.duration_ms, em.title, "
                       "       EXISTS("
                       "           SELECT 1 FROM track_issues ti "
                       "           WHERE ti.track_id = t.id "
                       "             AND ti.kind = 'needs_online' "
                       "             AND ti.field = 'title'"
                       "       ) AS title_unusable, "
                       "       em.artist, em.album_artist, em.year, "
                       "       em.track_number, em.disc_number, em.track_total, em.disc_total "
                       "FROM tracks t "
                       "JOIN files f ON f.id = t.file_id "
                       "LEFT JOIN effective_metadata em ON em.track_id = t.id "
                       "WHERE t.album_id = ?;"));
    q.addBindValue(albumId);
    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }

    QList<RawTrackData> rawTracks;
    QStringList dirPaths;

    while (q.next()) {
        auto raw = readTrackRow(q);
        const QString dirPath = QFileInfo(raw.filePath).absolutePath();
        if (!dirPath.isEmpty()) {
            dirPaths.append(dirPath);
        }
        rawTracks.append(std::move(raw));
    }

    const QString commonDir = longestCommonParentDirectory(dirPaths);
    const QDir baseDir(commonDir);

    QList<AlbumInfoTrack> tracks;
    tracks.reserve(rawTracks.size());
    for (auto &raw : rawTracks) {
        raw.track.relativePath = commonDir.isEmpty() ? QFileInfo(raw.filePath).fileName()
                                                     : baseDir.relativeFilePath(raw.filePath);
        tracks.append(std::move(raw.track));
    }

    std::ranges::stable_sort(tracks, compareAlbumInfoTracks);
    outDirectory = commonDir;

    return tracks;
}

} // namespace

AlbumInfoSource::AlbumInfoSource(library::Database &db)
    : m_db(db)
{
}

core::Result<QList<QPair<qint64, int>>> AlbumInfoSource::pendingAlbumTrackCounts() const
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT a.id, COUNT(t.id) AS track_count "
                             "FROM albums a "
                             "JOIN tracks t ON t.album_id = a.id "
                             "WHERE NOT EXISTS ("
                             "    SELECT 1 FROM album_info_checks c WHERE c.album_id = a.id"
                             ") "
                             "AND EXISTS ("
                             "    SELECT 1 FROM tracks t2 "
                             "    LEFT JOIN effective_metadata em ON em.track_id = t2.id "
                             "    WHERE t2.album_id = a.id "
                             "      AND ("
                             "          em.artist IS NULL OR em.artist = '' "
                             "          OR em.album_artist IS NULL OR em.album_artist = '' "
                             "          OR em.year IS NULL OR em.year <= 0 "
                             "          OR em.track_number IS NULL OR em.track_number <= 0 "
                             "          OR EXISTS ("
                             "              SELECT 1 FROM track_issues ti "
                             "              WHERE ti.track_id = t2.id "
                             "                AND ti.kind = 'needs_online' "
                             "                AND ti.field = 'title'"
                             "          )"
                             "      )"
                             ") "
                             "GROUP BY a.id "
                             "ORDER BY a.id ASC;"));

    if (!q.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = q.lastError().text(),
            .detail = q.lastQuery(),
        };
    }

    QList<QPair<qint64, int>> results;
    while (q.next()) {
        results.append(qMakePair(q.value(0).toLongLong(), q.value(1).toInt()));
    }
    return results;
}

core::Result<QList<qint64>> AlbumInfoSource::pendingAlbums() const
{
    const auto countsRes = pendingAlbumTrackCounts();
    if (!countsRes.ok()) {
        return countsRes.error();
    }
    QList<qint64> ids;
    ids.reserve(countsRes.value().size());
    for (const auto &pair : countsRes.value()) {
        ids.append(pair.first);
    }
    return ids;
}

core::Result<QList<AlbumInfoInput>> AlbumInfoSource::load(const QList<qint64> &albumIds) const
{
    if (albumIds.isEmpty()) {
        return QList<AlbumInfoInput>();
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QList<AlbumInfoInput> inputs;
    inputs.reserve(albumIds.size());

    for (const qint64 albumId : albumIds) {
        const auto albumMetaRes = queryAlbumMetadata(conn, albumId);
        if (!albumMetaRes.ok()) {
            return albumMetaRes.error();
        }

        QString directory;
        const auto tracksRes = queryTracksForAlbum(conn, albumId, directory);
        if (!tracksRes.ok()) {
            return tracksRes.error();
        }

        AlbumInfoInput input;
        input.albumId = albumId;
        input.title = albumMetaRes.value().first;
        input.albumArtist = albumMetaRes.value().second;
        input.directory = std::move(directory);
        input.tracks = tracksRes.value();

        inputs.append(std::move(input));
    }

    return inputs;
}

core::Result<void> AlbumInfoSource::markChecked(
    const QList<qint64> &albumIds, const QString &model, int promptVersion, qint64 now) const
{
    if (albumIds.isEmpty()) {
        return { };
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    library::Transaction tx(conn, library::Transaction::Mode::Immediate);
    if (!tx.isActive()) {
        return core::Error {
            .code = QString(library::errc::kDbTransaction),
            .message = QStringLiteral("Failed to begin transaction for markChecked"),
            .detail = { },
        };
    }

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO album_info_checks (album_id, checked_at, model, prompt_version) "
        "VALUES (?, ?, ?, ?);"));

    for (const qint64 albumId : albumIds) {
        q.addBindValue(albumId);
        q.addBindValue(now);
        q.addBindValue(model);
        q.addBindValue(promptVersion);

        if (!q.exec()) {
            return core::Error {
                .code = QString(library::errc::kDbQuery),
                .message = q.lastError().text(),
                .detail = q.lastQuery(),
            };
        }
    }

    return tx.commit();
}

QStringList planAlbumInfoBatches(
    const QList<QPair<qint64, int>> &albumTrackCounts, int maxTracks, int maxAlbums)
{
    QStringList batches;
    QList<qint64> currentBatch;
    int currentTrackCount = 0;

    auto flushCurrent = [&]() {
        if (currentBatch.isEmpty()) {
            return;
        }
        QJsonArray arr;
        for (const qint64 id : currentBatch) {
            arr.append(id);
        }
        batches.append(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        currentBatch.clear();
        currentTrackCount = 0;
    };

    for (const auto &pair : albumTrackCounts) {
        const qint64 albumId = pair.first;
        const int trackCount = pair.second;

        if (trackCount > maxTracks) {
            flushCurrent();
            QJsonArray arr;
            arr.append(albumId);
            batches.append(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
        } else {
            if (!currentBatch.isEmpty()
                && (currentTrackCount + trackCount > maxTracks
                    || currentBatch.size() + 1 > maxAlbums)) {
                flushCurrent();
            }
            currentBatch.append(albumId);
            currentTrackCount += trackCount;
        }
    }

    flushCurrent();
    return batches;
}

core::Result<QList<qint64>> parseAlbumInfoItemKey(const QString &itemKey)
{
    const auto doc = QJsonDocument::fromJson(itemKey.toUtf8());
    if (!doc.isArray()) {
        return core::Error {
            .code = QString(errc::kAlbumInfoInvalidKey),
            .message = QStringLiteral("Expected JSON array for album_info itemKey"),
            .detail = itemKey,
        };
    }

    const QJsonArray arr = doc.array();
    if (arr.isEmpty()) {
        return core::Error {
            .code = QString(errc::kAlbumInfoInvalidKey),
            .message = QStringLiteral("Empty array in album_info itemKey"),
            .detail = itemKey,
        };
    }

    QList<qint64> ids;
    ids.reserve(arr.size());
    for (const auto &val : arr) {
        if (!val.isDouble()) {
            return core::Error {
                .code = QString(errc::kAlbumInfoInvalidKey),
                .message = QStringLiteral("Non-integer value in album_info itemKey"),
                .detail = itemKey,
            };
        }
        const qint64 id = val.toInteger(0);
        if (id <= 0) {
            return core::Error {
                .code = QString(errc::kAlbumInfoInvalidKey),
                .message = QStringLiteral("Invalid album ID in album_info itemKey"),
                .detail = itemKey,
            };
        }
        ids.append(id);
    }
    return ids;
}

} // namespace linernotes::butler
