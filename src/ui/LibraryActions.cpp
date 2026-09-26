// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <library/PlaylistStore.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/Format.h>
#include <ui/LibraryActions.h>
#include <ui/UiLogging.h>

#include <algorithm>

namespace linernotes::ui {

LibraryActions::LibraryActions(library::Database &db, player::Player &player, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_player(player)
{
}

void LibraryActions::playTracks(const QList<qint64> &trackIds, int startIndex)
{
    if (trackIds.isEmpty() || !m_db.isOpen()) {
        return;
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for playTracks");
        return;
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.tracksByIds(trackIds);
    if (!res.ok() || res.value().isEmpty()) {
        return;
    }

    const auto &rows = res.value();
    QList<player::QueueItem> items;
    items.reserve(rows.size());
    QSet<qint64> validIds;
    validIds.reserve(rows.size());

    for (const auto &row : rows) {
        items.append(player::QueueItem {
            .source = row.path,
            .trackId = row.trackId,
        });
        validIds.insert(row.trackId);
    }

    // Calculate mapped startIndex based on skipped tracks
    int mappedIndex = 0;
    const int effectiveStartIndex = std::clamp(startIndex, 0, static_cast<int>(trackIds.size()));
    for (int i = 0; i < effectiveStartIndex; ++i) {
        if (validIds.contains(trackIds.at(i))) {
            ++mappedIndex;
        }
    }
    mappedIndex = std::clamp(mappedIndex, 0, static_cast<int>(items.size()) - 1);

    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return;
    }

    queue->setItems(items, mappedIndex);
    m_player.playIndex(mappedIndex);
}

void LibraryActions::playNext(const QList<qint64> &trackIds)
{
    if (trackIds.isEmpty() || !m_db.isOpen()) {
        return;
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for playNext");
        return;
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.tracksByIds(trackIds);
    if (!res.ok() || res.value().isEmpty()) {
        return;
    }

    const auto &rows = res.value();
    QList<player::QueueItem> items;
    items.reserve(rows.size());
    for (const auto &row : rows) {
        items.append(player::QueueItem {
            .source = row.path,
            .trackId = row.trackId,
        });
    }

    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return;
    }

    queue->insertNext(items);
}

void LibraryActions::enqueue(const QList<qint64> &trackIds)
{
    if (trackIds.isEmpty() || !m_db.isOpen()) {
        return;
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for enqueue");
        return;
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.tracksByIds(trackIds);
    if (!res.ok() || res.value().isEmpty()) {
        return;
    }

    const auto &rows = res.value();
    QList<player::QueueItem> items;
    items.reserve(rows.size());
    for (const auto &row : rows) {
        items.append(player::QueueItem {
            .source = row.path,
            .trackId = row.trackId,
        });
    }

    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return;
    }

    queue->append(items);
}

QVariantMap LibraryActions::albumInfo(qint64 albumId) const
{
    if (albumId <= 0 || !m_db.isOpen()) {
        return { };
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for albumInfo");
        return { };
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.album(albumId);
    if (!res.ok() || !res.value().has_value()) {
        return { };
    }

    const auto &row = res.value().value();
    QVariantMap map;
    map.insert(QStringLiteral("albumId"), row.albumId);
    map.insert(QStringLiteral("title"), row.title);
    map.insert(QStringLiteral("albumArtist"), row.albumArtist);
    if (row.year.has_value()) {
        map.insert(QStringLiteral("year"), row.year.value());
    } else {
        map.insert(QStringLiteral("year"), QVariant());
    }
    map.insert(QStringLiteral("trackCount"), row.trackCount);
    map.insert(QStringLiteral("durationText"), formatDuration(row.totalDurationMs));
    map.insert(QStringLiteral("coverHash"), row.coverHash);
    return map;
}

QVariantMap LibraryActions::artistInfo(qint64 artistId) const
{
    if (artistId <= 0 || !m_db.isOpen()) {
        return { };
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for artistInfo");
        return { };
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.artist(artistId);
    if (!res.ok() || !res.value().has_value()) {
        return { };
    }

    const auto &row = res.value().value();
    QVariantMap map;
    map.insert(QStringLiteral("artistId"), row.artistId);
    map.insert(QStringLiteral("name"), row.name);
    map.insert(QStringLiteral("trackCount"), row.trackCount);
    map.insert(QStringLiteral("albumCount"), row.albumCount);
    map.insert(QStringLiteral("coverHash"), row.coverHash);
    return map;
}

void LibraryActions::showInFileManager(qint64 trackId) const
{
    if (trackId <= 0 || !m_db.isOpen()) {
        return;
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for showInFileManager");
        return;
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.tracksByIds({ trackId });
    if (!res.ok() || res.value().isEmpty()) {
        qCWarning(
            lcUi, "Track not found for showInFileManager: %lld", static_cast<long long>(trackId));
        return;
    }

    const QString &filePath = res.value().constFirst().path;
    const QFileInfo fileInfo(filePath);
    const QString dirPath = fileInfo.dir().absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

qint64 LibraryActions::saveQueueAsPlaylist(const QString &name)
{
    if (!m_db.isOpen()) {
        qCWarning(lcUi, "Database not open for saveQueueAsPlaylist");
        return 0;
    }

    auto *queue = m_player.queue();
    if (queue == nullptr) {
        qCWarning(lcUi, "Queue not available for saveQueueAsPlaylist");
        return 0;
    }

    QList<qint64> trackIds;
    const int count = queue->count();
    trackIds.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto &item = queue->at(i);
        if (item.trackId >= 0) {
            trackIds.append(item.trackId);
        }
    }

    library::PlaylistStore store(m_db);
    const auto res = store.createManual(name, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to save queue as playlist: %s", qPrintable(res.error().toString()));
        return 0;
    }
    return res.value();
}

} // namespace linernotes::ui
