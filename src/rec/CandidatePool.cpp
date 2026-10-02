// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CandidatePool.h"

#include <QHash>
#include <QList>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

#include <library/Database.h>
#include <rec/Errors.h>

#include <utility>

namespace linernotes::rec {

core::Result<QList<Candidate>> loadCandidates(library::Database &db)
{
    const auto connRes = db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }
    const auto &conn = connRes.value();

    QSqlQuery qArtists(conn);
    if (!qArtists.exec(QStringLiteral("SELECT ta.track_id, ta.artist_id "
                                      "FROM track_artists ta "
                                      "WHERE ta.role = 'artist' "
                                      "ORDER BY ta.track_id ASC, ta.position ASC;"))) {
        return core::Error {
            .code = QString::fromLatin1(errc::kRecDbFailed),
            .message = QStringLiteral("Failed to query track artists"),
            .detail = qArtists.lastError().text(),
        };
    }

    QHash<qint64, QList<qint64>> artistMap;
    while (qArtists.next()) {
        const qint64 trackId = qArtists.value(0).toLongLong();
        const qint64 artistId = qArtists.value(1).toLongLong();
        auto it = artistMap.find(trackId);
        if (it == artistMap.end()) {
            it = artistMap.insert(trackId, { });
        }
        it->append(artistId);
    }

    QSqlQuery q(conn);
    const QString sql = QStringLiteral(
        "SELECT "
        "  ts.track_id, "
        "  ts.album_id, "
        "  t.work_id, "
        "  ts.year, "
        "  COALESCE(tps.play_count, 0) AS play_count, "
        "  COALESCE(tps.skip_count, 0) AS skip_count, "
        "  tps.last_played_at, "
        "  (CASE WHEN fav_t.entity_id IS NOT NULL "
        "        OR (ts.album_id IS NOT NULL AND fav_a.entity_id IS NOT NULL) "
        "        OR fav_art.track_id IS NOT NULL THEN 1 ELSE 0 END) AS is_favorite "
        "FROM track_sort ts "
        "JOIN tracks t ON t.id = ts.track_id "
        "LEFT JOIN track_play_stats tps ON tps.track_id = ts.track_id "
        "LEFT JOIN favorites fav_t ON fav_t.entity_type = 'track' AND fav_t.entity_id = "
        "ts.track_id "
        "LEFT JOIN favorites fav_a ON fav_a.entity_type = 'album' AND fav_a.entity_id = "
        "ts.album_id "
        "LEFT JOIN ( "
        "  SELECT DISTINCT ta.track_id "
        "  FROM track_artists ta "
        "  JOIN favorites fav ON fav.entity_type = 'artist' AND fav.entity_id = ta.artist_id "
        "  WHERE ta.role = 'artist' "
        ") fav_art ON fav_art.track_id = ts.track_id "
        "WHERE ts.visible = 1 "
        "  AND NOT EXISTS ( "
        "    SELECT 1 FROM duplicate_members dm "
        "    WHERE dm.track_id = ts.track_id AND dm.recommended = 0 "
        "  ) "
        "ORDER BY ts.track_id ASC;");

    if (!q.exec(sql)) {
        return core::Error {
            .code = QString::fromLatin1(errc::kRecDbFailed),
            .message = QStringLiteral("Failed to query candidates"),
            .detail = q.lastError().text(),
        };
    }

    QList<Candidate> candidates;
    while (q.next()) {
        Candidate c;
        c.trackId = q.value(0).toLongLong();

        if (!q.value(1).isNull()) {
            c.albumId = q.value(1).toLongLong();
        }
        if (!q.value(2).isNull()) {
            c.workId = q.value(2).toLongLong();
        }
        if (!q.value(3).isNull()) {
            c.year = q.value(3).toInt();
        }

        c.playCount = q.value(4).toInt();
        c.skipCount = q.value(5).toInt();

        if (!q.value(6).isNull()) {
            c.lastPlayedAtMs = q.value(6).toLongLong();
        }

        c.favorite = (q.value(7).toInt() != 0);
        c.artistIds = artistMap.value(c.trackId);

        candidates.append(std::move(c));
    }

    return candidates;
}

} // namespace linernotes::rec
