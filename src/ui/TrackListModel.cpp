// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFileInfo>

#include <ui/Format.h>
#include <ui/PlaylistController.h>
#include <ui/TrackListModel.h>
#include <ui/UiLogging.h>

#include <algorithm>

namespace linernotes::ui {

TrackListModel::TrackListModel(QObject *parent)
    : PagedListModel(parent)
{
}

void TrackListModel::setContext(AppContext *context)
{
    if (this->context() != nullptr) {
        disconnect(this->context()->playlists(), &PlaylistController::playlistContentChanged, this,
            &TrackListModel::onPlaylistContentChanged);
        disconnect(this->context()->playlists(), &PlaylistController::playlistsChanged, this,
            &TrackListModel::onPlaylistsChanged);
    }

    PagedListModel::setContext(context);

    if (this->context() != nullptr) {
        connect(this->context()->playlists(), &PlaylistController::playlistContentChanged, this,
            &TrackListModel::onPlaylistContentChanged);
        connect(this->context()->playlists(), &PlaylistController::playlistsChanged, this,
            &TrackListModel::onPlaylistsChanged);
    }
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || index.column() != 0) {
        return { };
    }

    const auto queryOpt = makeQuery();
    if (!queryOpt.has_value()) {
        return { };
    }

    auto loader = [this, query = queryOpt.value()](
                      int offset, int limit) -> core::Result<QList<library::TrackRow>> {
        auto res = query.tracks(currentFilter(), static_cast<library::TrackSortKey>(m_sortKey),
            sortOrder(), offset, limit);
        if (!res.ok()) {
            const int page = pageSize() > 0 ? (offset / pageSize()) : 0;
            warnOnce(page,
                QStringLiteral("Failed to load tracks page at offset %1: %2")
                    .arg(offset)
                    .arg(res.error().toString()));
        }
        return res;
    };

    const library::TrackRow *row = m_cache.row(index.row(), loader);
    if (row == nullptr) {
        return { };
    }

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: {
        if (!row->title.isEmpty()) {
            return row->title;
        }
        const QString base = QFileInfo(row->path).completeBaseName();
        return base.isEmpty() ? row->path : base;
    }
    case TrackIdRole:
        return row->trackId;
    case PathRole:
        return row->path;
    case ArtistRole:
        return row->artist;
    case AlbumRole:
        return row->album;
    case AlbumArtistRole:
        return row->albumArtist;
    case GenreRole:
        return row->genre;
    case YearRole:
        return row->year.has_value() ? QVariant(row->year.value()) : QVariant();
    case TrackNumberRole:
        return row->trackNumber.has_value() ? QVariant(row->trackNumber.value()) : QVariant();
    case DiscNumberRole:
        return row->discNumber.has_value() ? QVariant(row->discNumber.value()) : QVariant();
    case DurationMsRole:
        return row->durationMs;
    case DurationTextRole:
        return formatDuration(row->durationMs);
    case CodecRole:
        return row->codec;
    case SampleRateRole:
        return row->sampleRate;
    case BitDepthRole:
        return row->bitDepth;
    case BitrateRole:
        return row->bitrate;
    case CoverHashRole:
        return row->coverHash;
    case FavoriteRole:
        return row->favorite;
    case RatingRole:
        return row->rating;
    case AddedAtRole:
        return row->addedAt;
    default:
        return { };
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        { TrackIdRole, "trackId" },
        { PathRole, "path" },
        { TitleRole, "title" },
        { ArtistRole, "artist" },
        { AlbumRole, "album" },
        { AlbumArtistRole, "albumArtist" },
        { GenreRole, "genre" },
        { YearRole, "year" },
        { TrackNumberRole, "trackNumber" },
        { DiscNumberRole, "discNumber" },
        { DurationMsRole, "durationMs" },
        { DurationTextRole, "durationText" },
        { CodecRole, "codec" },
        { SampleRateRole, "sampleRate" },
        { BitDepthRole, "bitDepth" },
        { BitrateRole, "bitrate" },
        { CoverHashRole, "coverHash" },
        { FavoriteRole, "favorite" },
        { RatingRole, "rating" },
        { AddedAtRole, "addedAt" },
    };
}

TrackListModel::SortKey TrackListModel::sortKey() const
{
    return m_sortKey;
}

void TrackListModel::setSortKey(SortKey key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    emit sortKeyChanged(key);
    reload();
}

qint64 TrackListModel::albumId() const
{
    return m_albumId;
}

void TrackListModel::setAlbumId(qint64 albumId)
{
    if (m_albumId == albumId) {
        return;
    }
    m_albumId = albumId;
    emit albumIdChanged(albumId);
    reload();
}

qint64 TrackListModel::artistId() const
{
    return m_artistId;
}

void TrackListModel::setArtistId(qint64 artistId)
{
    if (m_artistId == artistId) {
        return;
    }
    m_artistId = artistId;
    emit artistIdChanged(artistId);
    reload();
}

QString TrackListModel::genre() const
{
    return m_genre;
}

void TrackListModel::setGenre(const QString &genre)
{
    if (m_genre == genre) {
        return;
    }
    m_genre = genre;
    emit genreChanged(genre);
    reload();
}

qint64 TrackListModel::playlistId() const
{
    return m_playlistId;
}

void TrackListModel::setPlaylistId(qint64 playlistId)
{
    if (m_playlistId == playlistId) {
        return;
    }
    m_playlistId = playlistId;
    emit playlistIdChanged(playlistId);
    reload();
}

void TrackListModel::onPlaylistContentChanged(qint64 id)
{
    if (m_playlistId > 0 && m_playlistId == id) {
        reload();
    }
}

void TrackListModel::onPlaylistsChanged()
{
    if (m_playlistId > 0) {
        reload();
    }
}

qint64 TrackListModel::trackIdAt(int row) const
{
    if (row < 0 || row >= count()) {
        return 0;
    }
    const auto queryOpt = makeQuery();
    if (!queryOpt.has_value()) {
        return 0;
    }

    auto loader = [this, query = queryOpt.value()](
                      int offset, int limit) -> core::Result<QList<library::TrackRow>> {
        auto res = query.tracks(currentFilter(), static_cast<library::TrackSortKey>(m_sortKey),
            sortOrder(), offset, limit);
        if (!res.ok()) {
            const int page = pageSize() > 0 ? (offset / pageSize()) : 0;
            warnOnce(page,
                QStringLiteral("Failed to load tracks page at offset %1: %2")
                    .arg(offset)
                    .arg(res.error().toString()));
        }
        return res;
    };

    const library::TrackRow *r = m_cache.row(row, loader);
    return r != nullptr ? r->trackId : 0;
}

QList<qint64> TrackListModel::trackIds(const QList<int> &rows) const
{
    QList<qint64> result;
    result.reserve(rows.size());
    for (const int r : rows) {
        const qint64 id = trackIdAt(r);
        if (id > 0) {
            result.append(id);
        }
    }
    return result;
}

QList<qint64> TrackListModel::allTrackIds() const
{
    const auto queryOpt = makeQuery();
    if (!queryOpt.has_value()) {
        return { };
    }

    const auto res = queryOpt->trackIds(
        currentFilter(), static_cast<library::TrackSortKey>(m_sortKey), sortOrder());
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to get all track ids: %s", qPrintable(res.error().toString()));
        return { };
    }
    return res.value();
}

core::Result<int> TrackListModel::queryCount(const library::LibraryQuery &query) const
{
    return query.countTracks(currentFilter());
}

void TrackListModel::clearCache()
{
    m_cache.setPageSize(pageSize());
    m_cache.clear();
}

library::TrackFilter TrackListModel::currentFilter() const
{
    library::TrackFilter filter;
    if (m_albumId > 0) {
        filter.albumId = m_albumId;
    }
    if (m_artistId > 0) {
        filter.artistId = m_artistId;
    }
    if (!m_genre.isEmpty()) {
        filter.genre = m_genre;
    }
    if (m_playlistId > 0) {
        // 歌单已删除或 context 未就绪时用一个不存在的 id，返回空结果
        filter.playlistId = -1;
        const auto info
            = context() != nullptr ? context()->playlists()->info(m_playlistId) : std::nullopt;
        if (info.has_value() && info->kind == library::PlaylistKind::Manual) {
            filter.playlistId = m_playlistId;
        } else if (info.has_value() && info->kind == library::PlaylistKind::Smart) {
            filter.playlistId.reset();
            filter.smartRule = info->rule.value_or(library::SmartRule { });
        }
    }
    filter.favoritesOnly = favoritesOnly();
    return filter;
}

} // namespace linernotes::ui
