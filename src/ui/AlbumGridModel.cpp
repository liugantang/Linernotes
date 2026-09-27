// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ui/AlbumGridModel.h>
#include <ui/UiLogging.h>

#include <algorithm>

namespace linernotes::ui {

namespace {

QString formatDuration(qint64 durationMs)
{
    const qint64 totalSeconds = std::max(0LL, durationMs) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

AlbumGridModel::AlbumGridModel(QObject *parent)
    : PagedListModel(parent)
{
}

QVariant AlbumGridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || index.column() != 0) {
        return { };
    }

    const auto queryOpt = makeQuery();
    if (!queryOpt.has_value()) {
        return { };
    }

    auto loader = [this, query = queryOpt.value()](
                      int offset, int limit) -> core::Result<QList<library::AlbumRow>> {
        auto res = query.albums(currentFilter(), static_cast<library::AlbumSortKey>(m_sortKey),
            sortOrder(), offset, limit);
        if (!res.ok()) {
            const int page = pageSize() > 0 ? (offset / pageSize()) : 0;
            warnOnce(page,
                QStringLiteral("Failed to load albums page at offset %1: %2")
                    .arg(offset)
                    .arg(res.error().toString()));
        }
        return res;
    };

    const library::AlbumRow *row = m_cache.row(index.row(), loader);
    if (row == nullptr) {
        return { };
    }

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return row->title;
    case AlbumIdRole:
        return row->albumId;
    case AlbumArtistRole:
        return row->albumArtist;
    case YearRole:
        return row->year.has_value() ? QVariant(row->year.value()) : QVariant();
    case TrackCountRole:
        return row->trackCount;
    case DurationMsRole:
        return row->totalDurationMs;
    case DurationTextRole:
        return formatDuration(row->totalDurationMs);
    case CoverHashRole:
        return row->coverHash;
    case FavoriteRole:
        return row->favorite;
    default:
        return { };
    }
}

QHash<int, QByteArray> AlbumGridModel::roleNames() const
{
    return {
        { AlbumIdRole, "albumId" },
        { TitleRole, "title" },
        { AlbumArtistRole, "albumArtist" },
        { YearRole, "year" },
        { TrackCountRole, "trackCount" },
        { DurationMsRole, "durationMs" },
        { DurationTextRole, "durationText" },
        { CoverHashRole, "coverHash" },
        { FavoriteRole, "favorite" },
    };
}

AlbumGridModel::SortKey AlbumGridModel::sortKey() const
{
    return m_sortKey;
}

void AlbumGridModel::setSortKey(SortKey key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    emit sortKeyChanged(key);
    reload();
}

qint64 AlbumGridModel::artistId() const
{
    return m_artistId;
}

void AlbumGridModel::setArtistId(qint64 artistId)
{
    if (m_artistId == artistId) {
        return;
    }
    m_artistId = artistId;
    emit artistIdChanged(artistId);
    reload();
}

core::Result<int> AlbumGridModel::queryCount(const library::LibraryQuery &query) const
{
    return query.countAlbums(currentFilter());
}

void AlbumGridModel::clearCache()
{
    m_cache.setPageSize(pageSize());
    m_cache.clear();
}

library::AlbumFilter AlbumGridModel::currentFilter() const
{
    library::AlbumFilter filter;
    if (m_artistId > 0) {
        filter.artistId = m_artistId;
    }
    filter.favoritesOnly = favoritesOnly();
    return filter;
}

} // namespace linernotes::ui
