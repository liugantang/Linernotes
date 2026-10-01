// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LibraryQuerySql.h"

#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <library/EnumNames.h>

namespace linernotes::library::detail {

TrackRow parseTrackRow(const QSqlQuery &q)
{
    TrackRow row;
    row.trackId = q.value(0).toLongLong();
    row.fileId = q.value(1).toLongLong();
    if (!q.value(2).isNull()) {
        row.albumId = q.value(2).toLongLong();
    }
    row.path = q.value(3).toString();
    row.title = q.value(4).toString();
    row.artist = q.value(5).toString();
    row.album = q.value(6).toString();
    row.albumArtist = q.value(7).toString();
    row.genre = q.value(8).toString();
    row.composer = q.value(9).toString();
    if (!q.value(10).isNull()) {
        row.year = q.value(10).toInt();
    }
    if (!q.value(11).isNull()) {
        row.trackNumber = q.value(11).toInt();
    }
    if (!q.value(12).isNull()) {
        row.discNumber = q.value(12).toInt();
    }
    row.durationMs = q.value(13).toLongLong();
    row.codec = q.value(14).toString();
    row.sampleRate = q.value(15).toInt();
    row.bitDepth = q.value(16).toInt();
    row.bitrate = q.value(17).toInt();
    row.coverHash = q.value(18).toString();
    row.favorite = q.value(19).toInt() != 0;
    row.rating = q.value(20).toInt();
    row.addedAt = q.value(21).toLongLong();
    row.playCount = q.value(22).toInt();
    if (!q.value(23).isNull()) {
        row.lastPlayedAtMs = q.value(23).toLongLong();
    }
    if (!q.value(24).isNull()) {
        row.versionType = versionTypeFromString(q.value(24).toString());
    }
    row.titleTranslated = q.value(25).toString();
    row.albumTranslated = q.value(26).toString();
    return row;
}

AlbumRow parseAlbumRow(const QSqlQuery &q)
{
    AlbumRow row;
    row.albumId = q.value(0).toLongLong();
    row.title = q.value(1).toString();
    row.albumArtist = q.value(2).toString();
    if (!q.value(3).isNull()) {
        row.year = q.value(3).toInt();
    }
    row.trackCount = q.value(4).toInt();
    row.totalDurationMs = q.value(5).toLongLong();
    row.coverHash = q.value(6).toString();
    row.favorite = q.value(7).toInt() != 0;
    row.titleTranslated = q.value(8).toString();
    return row;
}

ArtistRow parseArtistRow(const QSqlQuery &q)
{
    ArtistRow row;
    row.artistId = q.value(0).toLongLong();
    row.name = q.value(1).toString();
    row.originalName = q.value(2).toString();
    row.trackCount = q.value(3).toInt();
    row.albumCount = q.value(4).toInt();
    row.coverHash = q.value(5).toString();
    row.favorite = q.value(6).toInt() != 0;
    return row;
}

namespace {

// playlistId 是整数，直接嵌入 SQL 不存在注入风险
OrderClauses buildPlaylistOrderClauses(qint64 playlistId, bool isAsc)
{
    const QString pidStr = QString::number(playlistId);
    auto makeSubquery = [&](const QString &trackIdExpr) {
        return QStringLiteral("(SELECT pi.position FROM playlist_items pi WHERE pi.playlist_id = "
                              "%1 AND pi.track_id = %2)")
            .arg(pidStr, trackIdExpr);
    };

    const QString dir = isAsc ? QStringLiteral("ASC") : QStringLiteral("DESC");
    const QString pageExpr = makeSubquery(QStringLiteral("ts.track_id"));
    const QString outerExpr = makeSubquery(QStringLiteral("t.id"));

    return OrderClauses {
        .pageOrder = QStringLiteral("%1 %2, ts.track_id %2").arg(pageExpr, dir),
        .outerOrder = QStringLiteral("%1 %2, t.id %2").arg(outerExpr, dir),
    };
}

} // namespace

OrderClauses buildTrackOrderClauses(TrackSortKey key, bool isAsc, std::optional<qint64> playlistId)
{
    OrderClauses clauses;
    switch (key) {
    case TrackSortKey::PlaylistOrder:
        if (playlistId.has_value()) {
            return buildPlaylistOrderClauses(playlistId.value(), isAsc);
        }
        [[fallthrough]];

    case TrackSortKey::Default:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("ts.album_artist_key IS NULL, ts.album_artist_key ASC, "
                                 "ts.album IS NULL, ts.album ASC, "
                                 "ts.disc_number IS NULL, ts.disc_number ASC, "
                                 "ts.track_number IS NULL, ts.track_number ASC, "
                                 "ts.path ASC, ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("COALESCE(em.album_artist, em.artist) IS NULL, "
                                                "COALESCE(em.album_artist, em.artist) ASC, "
                                                "em.album IS NULL, em.album ASC, "
                                                "em.disc_number IS NULL, em.disc_number ASC, "
                                                "em.track_number IS NULL, em.track_number ASC, "
                                                "f.path ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.album_artist_key DESC, ts.album DESC, "
                                               "ts.disc_number DESC, ts.track_number DESC, "
                                               "ts.path DESC, ts.track_id DESC");
            clauses.outerOrder
                = QStringLiteral("COALESCE(em.album_artist, em.artist) DESC, em.album DESC, "
                                 "em.disc_number DESC, em.track_number DESC, "
                                 "f.path DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Title:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.title IS NULL, ts.title ASC, ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.title IS NULL, em.title ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.title DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("em.title DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Artist:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.artist IS NULL, ts.artist ASC, ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.artist IS NULL, em.artist ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.artist DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("em.artist DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Album:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.album IS NULL, ts.album ASC, "
                                               "ts.disc_number IS NULL, ts.disc_number ASC, "
                                               "ts.track_number IS NULL, ts.track_number ASC, "
                                               "ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.album IS NULL, em.album ASC, "
                                                "em.disc_number IS NULL, em.disc_number ASC, "
                                                "em.track_number IS NULL, em.track_number ASC, "
                                                "t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral(
                "ts.album DESC, ts.disc_number DESC, ts.track_number DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral(
                "em.album DESC, em.disc_number DESC, em.track_number DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Year:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("ts.year IS NULL, ts.year ASC, "
                                               "ts.album IS NULL, ts.album ASC, "
                                               "ts.disc_number IS NULL, ts.disc_number ASC, "
                                               "ts.track_number IS NULL, ts.track_number ASC, "
                                               "ts.track_id ASC");
            clauses.outerOrder = QStringLiteral("em.year IS NULL, em.year ASC, "
                                                "em.album IS NULL, em.album ASC, "
                                                "em.disc_number IS NULL, em.disc_number ASC, "
                                                "em.track_number IS NULL, em.track_number ASC, "
                                                "t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.year DESC, ts.album DESC, ts.disc_number DESC, "
                                               "ts.track_number DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("em.year DESC, em.album DESC, em.disc_number DESC, "
                                                "em.track_number DESC, t.id DESC");
        }
        break;

    case TrackSortKey::Duration:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("ts.duration_ms IS NULL, ts.duration_ms ASC, ts.track_id ASC");
            clauses.outerOrder
                = QStringLiteral("f.duration_ms IS NULL, f.duration_ms ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.duration_ms DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("f.duration_ms DESC, t.id DESC");
        }
        break;

    case TrackSortKey::DateAdded:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("ts.added_at IS NULL, ts.added_at ASC, ts.track_id ASC");
            clauses.outerOrder
                = QStringLiteral("f.first_seen_at IS NULL, f.first_seen_at ASC, t.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("ts.added_at DESC, ts.track_id DESC");
            clauses.outerOrder = QStringLiteral("f.first_seen_at DESC, t.id DESC");
        }
        break;
    }
    return clauses;
}

OrderClauses buildAlbumOrderClauses(AlbumSortKey key, bool isAsc)
{
    OrderClauses clauses;
    switch (key) {
    case AlbumSortKey::Title:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("a.title IS NULL, a.title ASC, a.id ASC");
            clauses.outerOrder = QStringLiteral("p.title IS NULL, p.title ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.title DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.title DESC, p.id DESC");
        }
        break;

    case AlbumSortKey::Artist:
        if (isAsc) {
            clauses.pageOrder
                = QStringLiteral("a.album_artist IS NULL, a.album_artist ASC, a.id ASC");
            clauses.outerOrder
                = QStringLiteral("p.album_artist IS NULL, p.album_artist ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.album_artist DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.album_artist DESC, p.id DESC");
        }
        break;

    case AlbumSortKey::Year:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("a.year IS NULL, a.year ASC, a.id ASC");
            clauses.outerOrder = QStringLiteral("p.year IS NULL, p.year ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.year DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.year DESC, p.id DESC");
        }
        break;

    case AlbumSortKey::DateAdded:
        if (isAsc) {
            clauses.pageOrder = QStringLiteral("a.created_at IS NULL, a.created_at ASC, a.id ASC");
            clauses.outerOrder = QStringLiteral("p.created_at IS NULL, p.created_at ASC, p.id ASC");
        } else {
            clauses.pageOrder = QStringLiteral("a.created_at DESC, a.id DESC");
            clauses.outerOrder = QStringLiteral("p.created_at DESC, p.id DESC");
        }
        break;
    }
    return clauses;
}

QString buildTrackFilterWhereSql(const TrackFilter &filter, QList<QVariant> &binds)
{
    QString whereSql;
    if (filter.albumId.has_value()) {
        whereSql += QStringLiteral(" AND ts.album_id = ?");
        binds.append(filter.albumId.value());
    }

    if (filter.artistId.has_value()) {
        whereSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM track_artists ta WHERE ta.track_id = ts.track_id AND "
            "ta.artist_id = ? AND ta.role = 'artist')");
        binds.append(filter.artistId.value());
    }

    if (filter.genre.has_value()) {
        whereSql += QStringLiteral(" AND ts.genre = ?");
        binds.append(filter.genre.value());
    }

    if (filter.favoritesOnly) {
        whereSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'track' AND "
            "fav.entity_id = ts.track_id)");
    }

    if (filter.playlistId.has_value()) {
        whereSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM playlist_items pi WHERE pi.track_id = ts.track_id AND "
            "pi.playlist_id = ?)");
        binds.append(filter.playlistId.value());
    }

    if (filter.smartRule.has_value()) {
        const auto &rule = filter.smartRule.value();
        if (rule.limit.has_value()) {
            QList<QVariant> innerBinds;
            const QString innerWhere = buildSmartRuleWhereSql(rule, innerBinds, PlayCountRule { });
            const bool innerAsc = (rule.sortOrder == Qt::AscendingOrder);
            const OrderClauses innerOrderClauses = buildTrackOrderClauses(rule.sortKey, innerAsc);

            // 内层别名 ts 会遮蔽外层 ts，内层条件与排序均解析到内层 track_sort 表
            whereSql += QStringLiteral(" AND ts.track_id IN (SELECT ts.track_id FROM track_sort ts "
                                       "WHERE ts.visible = 1%1 ORDER BY %2 LIMIT ?)")
                            .arg(innerWhere, innerOrderClauses.pageOrder);
            binds.append(innerBinds);
            binds.append(rule.limit.value());
        } else {
            whereSql += buildSmartRuleWhereSql(rule, binds, PlayCountRule { });
        }
    }

    return whereSql;
}

QString buildAlbumFilterWhereSql(const AlbumFilter &filter, QList<QVariant> &binds)
{
    QString filterSql;
    if (filter.artistId.has_value()) {
        filterSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM album_artists aa WHERE aa.album_id = a.id AND aa.artist_id "
            "= ?)");
        binds.append(filter.artistId.value());
    }

    if (filter.favoritesOnly) {
        filterSql += QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'album' AND "
            "fav.entity_id = a.id)");
    }
    return filterSql;
}

QString buildArtistFilterWhereSql(const ArtistFilter &filter)
{
    if (filter.favoritesOnly) {
        return QStringLiteral(
            " AND EXISTS (SELECT 1 FROM favorites fav WHERE fav.entity_type = 'artist' AND "
            "fav.entity_id = ar.id)");
    }
    return { };
}

QString trackSelectSql(const QString &fromSource)
{
    return QStringLiteral(
        "SELECT t.id, t.file_id, t.album_id, f.path, "
        "em.title, em.artist, em.album, em.album_artist, em.genre, em.composer, "
        "em.year, em.track_number, em.disc_number, "
        "f.duration_ms, f.codec, f.sample_rate, f.bit_depth, f.bitrate, "
        "c.hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav, "
        "COALESCE(r.rating, 0) AS rating, "
        "f.first_seen_at, "
        "COALESCE(tps.play_count, 0) AS play_count, "
        "tps.last_played_at, "
        "tv.version_type, "
        "tt_title.translated AS title_translated, "
        "tt_album.translated AS album_translated "
        "FROM %1 "
        "JOIN files f ON t.file_id = f.id "
        "LEFT JOIN effective_metadata em ON t.id = em.track_id "
        "LEFT JOIN covers c ON f.cover_id = c.id "
        "LEFT JOIN favorites fav ON fav.entity_type = 'track' AND fav.entity_id = t.id "
        "LEFT JOIN ratings r ON r.track_id = t.id "
        "LEFT JOIN track_play_stats tps ON tps.track_id = t.id "
        "LEFT JOIN track_versions tv ON tv.track_id = t.id "
        "LEFT JOIN text_translations tt_title ON tt_title.source_text = TRIM(em.title) AND "
        "tt_title.target_lang = 'zh-Hans' AND tt_title.translated <> '' "
        "LEFT JOIN text_translations tt_album ON tt_album.source_text = TRIM(em.album) AND "
        "tt_album.target_lang = 'zh-Hans' AND tt_album.translated <> ''")
        .arg(fromSource);
}

QString albumSelectSql(const QString &fromSource, const QString &alias)
{
    return QStringLiteral(
        "SELECT %1.id, %1.title, "
        "COALESCE(NULLIF(%1.album_artist, ''), (SELECT ts.artist FROM track_sort ts WHERE "
        "ts.album_id = %1.id AND ts.visible = 1 GROUP BY ts.artist ORDER BY COUNT(*) DESC, "
        "ts.artist ASC LIMIT 1)), "
        "%1.year, "
        "(SELECT COUNT(*) FROM track_sort ts WHERE ts.album_id = %1.id AND ts.visible = 1) AS "
        "track_count, "
        "(SELECT COALESCE(SUM(ts.duration_ms), 0) FROM track_sort ts WHERE ts.album_id = %1.id AND "
        "ts.visible = 1) AS total_duration, "
        "(SELECT c.hash FROM covers c WHERE c.id = %1.cover_id) AS cover_hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav, "
        "tt.translated AS title_translated "
        "FROM %2 "
        "LEFT JOIN favorites fav ON fav.entity_type = 'album' AND fav.entity_id = %1.id "
        "LEFT JOIN text_translations tt ON tt.source_text = TRIM(%1.title) AND "
        "tt.target_lang = 'zh-Hans' AND tt.translated <> ''")
        .arg(alias, fromSource);
}

QString artistVisibilityWhereSql(const QString &artistIdExpr)
{
    return QStringLiteral(
        "("
        "  EXISTS (SELECT 1 FROM track_artists ta JOIN track_sort ts ON ta.track_id = ts.track_id "
        "WHERE ta.artist_id = %1 AND ta.role = 'artist' AND ts.visible = 1)"
        "  OR EXISTS (SELECT 1 FROM album_artists aa JOIN track_sort ts ON aa.album_id = "
        "ts.album_id "
        "WHERE aa.artist_id = %1 AND ts.visible = 1)"
        ")")
        .arg(artistIdExpr);
}

namespace {

QString buildArtistSelectSql(const QString &fromSource, const QString &alias,
    const QString &nameExpr, const QString &origNameExpr)
{
    return QStringLiteral(
        "SELECT %1.id, %2 AS name, %3 AS original_name, "
        "(SELECT COUNT(DISTINCT ta.track_id) FROM track_artists ta JOIN track_sort ts ON "
        "ta.track_id = ts.track_id WHERE ta.artist_id = %1.id AND ta.role = 'artist' AND "
        "ts.visible = 1) AS track_count, "
        "(SELECT COUNT(DISTINCT a.id) FROM albums a JOIN album_artists aa ON a.id = aa.album_id "
        "WHERE aa.artist_id = %1.id AND EXISTS (SELECT 1 FROM track_sort ts WHERE ts.album_id = "
        "a.id AND ts.visible = 1)) AS album_count, "
        "(SELECT c.hash FROM albums a JOIN album_artists aa ON a.id = aa.album_id JOIN covers c ON "
        "a.cover_id = c.id WHERE aa.artist_id = %1.id AND a.cover_id IS NOT NULL AND EXISTS "
        "(SELECT 1 FROM track_sort ts WHERE ts.album_id = a.id AND ts.visible = 1) ORDER BY a.year "
        "ASC NULLS LAST, a.id ASC LIMIT 1) AS cover_hash, "
        "(fav.entity_id IS NOT NULL) AS is_fav "
        "FROM %4 "
        "LEFT JOIN favorites fav ON fav.entity_type = 'artist' AND fav.entity_id = %1.id")
        .arg(alias, nameExpr, origNameExpr, fromSource);
}

} // namespace

QString artistDisplayNameSql(const QString &alias, ArtistNamePreference pref)
{
    switch (pref) {
    case ArtistNamePreference::Original:
        return QStringLiteral("%1.name").arg(alias);
    case ArtistNamePreference::SimplifiedChinese:
        return QStringLiteral(
            "COALESCE((SELECT a.alias FROM artist_aliases a WHERE a.artist_id = %1.id AND a.locale "
            "IN ('zh_Hans','zh') ORDER BY a.locale = 'zh_Hans' DESC, a.id LIMIT 1), %1.name)")
            .arg(alias);
    case ArtistNamePreference::English:
        return QStringLiteral(
            "COALESCE((SELECT a.alias FROM artist_aliases a WHERE a.artist_id = %1.id AND a.locale "
            "IN ('en','en_US','en_GB') ORDER BY a.locale = 'en' DESC, a.id LIMIT 1), %1.name)")
            .arg(alias);
    }
    return QStringLiteral("%1.name").arg(alias);
}

QString artistSelectSql(const QString &fromSource, const QString &alias, ArtistNamePreference pref)
{
    const QString nameExpr = artistDisplayNameSql(alias, pref);
    const QString origNameExpr = QStringLiteral("%1.name").arg(alias);
    return buildArtistSelectSql(fromSource, alias, nameExpr, origNameExpr);
}

QString artistPageSelectSql(const QString &fromSource, const QString &alias)
{
    const QString nameExpr = QStringLiteral("%1.name").arg(alias);
    const QString origNameExpr = QStringLiteral("%1.original_name").arg(alias);
    return buildArtistSelectSql(fromSource, alias, nameExpr, origNameExpr);
}

} // namespace linernotes::library::detail
