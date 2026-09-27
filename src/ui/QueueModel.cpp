// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "QueueModel.h"

#include <QFileInfo>
#include <QSet>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/Format.h>

#include <algorithm>
#include <functional>

namespace linernotes::ui {

QueueModel::QueueModel(library::Database &db, player::Player &player, QObject *parent)
    : QIdentityProxyModel(parent)
    , m_db(db)
    , m_player(player)
{
    setSourceModel(player.queue());
}

void QueueModel::refresh()
{
    m_cache.clear();
    const int count = rowCount();
    if (count > 0) {
        emit dataChanged(index(0, 0), index(count - 1, 0));
    }
}

const QueueModel::CachedTrackInfo &QueueModel::trackInfo(
    qint64 trackId, const QString &source) const
{
    auto it = m_cache.find(trackId);
    if (it != m_cache.end()) {
        return it.value();
    }

    CachedTrackInfo info;
    bool found = false;
    if (m_db.isOpen()) {
        const auto connOpt = m_db.connection();
        if (connOpt.ok()) {
            const library::LibraryQuery query(connOpt.value());
            const auto res = query.tracksByIds({ trackId });
            if (res.ok() && !res.value().isEmpty()) {
                const auto &row = res.value().constFirst();
                info.title = !row.title.isEmpty()
                    ? row.title
                    : QFileInfo(row.path.isEmpty() ? source : row.path).fileName();
                info.artist = row.artist;
                info.durationText = formatDuration(row.durationMs);
                info.coverHash = row.coverHash;
                found = true;
            }
        }
    }
    if (!found) {
        info.title = QFileInfo(source).fileName();
        info.artist = QString();
        info.durationText = QString();
        info.coverHash = QString();
    }
    return *m_cache.insert(trackId, info);
}

QVariant QueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() != 0) {
        return { };
    }

    if (role != TitleRole && role != ArtistRole && role != DurationTextRole
        && role != CoverHashRole) {
        return QIdentityProxyModel::data(index, role);
    }

    const QModelIndex srcIndex = mapToSource(index);
    if (!srcIndex.isValid() || sourceModel() == nullptr) {
        return { };
    }

    const qint64 trackId
        = sourceModel()->data(srcIndex, player::PlayQueue::TrackIdRole).toLongLong();
    const QString source = sourceModel()->data(srcIndex, player::PlayQueue::SourceRole).toString();

    if (trackId < 0) {
        if (role == TitleRole) {
            return QFileInfo(source).fileName();
        }
        return QString();
    }

    const auto &info = trackInfo(trackId, source);

    switch (role) {
    case TitleRole:
        return info.title;
    case ArtistRole:
        return info.artist;
    case DurationTextRole:
        return info.durationText;
    case CoverHashRole:
        return info.coverHash;
    default:
        return { };
    }
}

QHash<int, QByteArray> QueueModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    if (sourceModel() != nullptr) {
        roles = sourceModel()->roleNames();
    } else {
        roles = QIdentityProxyModel::roleNames();
    }
    roles.insert(TitleRole, "title");
    roles.insert(ArtistRole, "artist");
    roles.insert(DurationTextRole, "durationText");
    roles.insert(CoverHashRole, "coverHash");
    return roles;
}

void QueueModel::playAt(int row)
{
    m_player.playIndex(row);
}

void QueueModel::move(int from, int to)
{
    auto *queue = m_player.queue();
    if (queue != nullptr) {
        queue->move(from, to);
    }
}

void QueueModel::removeItems(const QList<int> &rows)
{
    auto *queue = m_player.queue();
    if (queue == nullptr || rows.isEmpty()) {
        return;
    }

    const int current = queue->currentIndex();
    const int count = queue->count();

    // Skip the currently playing row to prevent the UI from getting out of sync with actual
    // playback.
    QSet<int> uniqueRows;
    for (const int r : rows) {
        if (r >= 0 && r < count && r != current) {
            uniqueRows.insert(r);
        }
    }

    QList<int> sortedRows = uniqueRows.values();
    std::ranges::sort(sortedRows, std::greater<>());
    for (const int r : sortedRows) {
        queue->remove(r, 1);
    }
}

void QueueModel::clear()
{
    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return;
    }

    const int current = queue->currentIndex();
    if (current < 0 || current >= queue->count()) {
        queue->clear();
        return;
    }

    // Preserve the currently playing track by removing everything after and before it.
    if (current + 1 < queue->count()) {
        queue->remove(current + 1, queue->count() - current - 1);
    }
    if (current > 0) {
        queue->remove(0, current);
    }
}

QList<qint64> QueueModel::trackIds() const
{
    QList<qint64> result;
    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return result;
    }

    const int count = queue->count();
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto &item = queue->at(i);
        if (item.trackId >= 0) {
            result.append(item.trackId);
        }
    }
    return result;
}

} // namespace linernotes::ui
