// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ui/ArtistListModel.h>
#include <ui/SettingsController.h>
#include <ui/UiLogging.h>

namespace linernotes::ui {

ArtistListModel::ArtistListModel(QObject *parent)
    : PagedListModel(parent)
{
}

void ArtistListModel::setContext(AppContext *context)
{
    if (this->context() != nullptr && this->context()->settings() != nullptr) {
        disconnect(this->context()->settings(), &SettingsController::artistNamePreferenceChanged,
            this, &ArtistListModel::onSettingsPreferenceChanged);
    }

    PagedListModel::setContext(context);

    if (this->context() != nullptr && this->context()->settings() != nullptr) {
        connect(this->context()->settings(), &SettingsController::artistNamePreferenceChanged, this,
            &ArtistListModel::onSettingsPreferenceChanged);
    }
}

void ArtistListModel::onSettingsPreferenceChanged()
{
    clearCache();
    reload();
}

QVariant ArtistListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || index.column() != 0) {
        return { };
    }

    const auto queryOpt = makeQuery();
    if (!queryOpt.has_value()) {
        return { };
    }

    auto loader = [this, query = queryOpt.value()](
                      int offset, int limit) -> core::Result<QList<library::ArtistRow>> {
        auto res = query.artists(currentFilter(), static_cast<library::ArtistSortKey>(m_sortKey),
            sortOrder(), offset, limit);
        if (!res.ok()) {
            const int page = pageSize() > 0 ? (offset / pageSize()) : 0;
            warnOnce(page,
                QStringLiteral("Failed to load artists page at offset %1: %2")
                    .arg(offset)
                    .arg(res.error().toString()));
        }
        return res;
    };

    const library::ArtistRow *row = m_cache.row(index.row(), loader);
    if (row == nullptr) {
        return { };
    }

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return row->name;
    case OriginalNameRole:
        return row->originalName;
    case ArtistIdRole:
        return row->artistId;
    case TrackCountRole:
        return row->trackCount;
    case AlbumCountRole:
        return row->albumCount;
    case CoverHashRole:
        return row->coverHash;
    case FavoriteRole:
        return row->favorite;
    default:
        return { };
    }
}

QHash<int, QByteArray> ArtistListModel::roleNames() const
{
    return {
        { ArtistIdRole, "artistId" },
        { NameRole, "name" },
        { OriginalNameRole, "originalName" },
        { TrackCountRole, "trackCount" },
        { AlbumCountRole, "albumCount" },
        { CoverHashRole, "coverHash" },
        { FavoriteRole, "favorite" },
    };
}

ArtistListModel::SortKey ArtistListModel::sortKey() const
{
    return m_sortKey;
}

void ArtistListModel::setSortKey(SortKey key)
{
    if (m_sortKey == key) {
        return;
    }
    m_sortKey = key;
    emit sortKeyChanged(key);
    reload();
}

core::Result<int> ArtistListModel::queryCount(const library::LibraryQuery &query) const
{
    return query.countArtists(currentFilter());
}

void ArtistListModel::clearCache()
{
    m_cache.setPageSize(pageSize());
    m_cache.clear();
}

library::ArtistFilter ArtistListModel::currentFilter() const
{
    library::ArtistFilter filter;
    filter.favoritesOnly = favoritesOnly();
    if (context() != nullptr && context()->settings() != nullptr) {
        filter.namePreference = context()->settings()->artistNamePreference();
    } else {
        filter.namePreference = library::ArtistNamePreference::Original;
    }
    return filter;
}

} // namespace linernotes::ui
