// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <library/Database.h>
#include <library/EnumNames.h>
#include <library/LibraryQuery.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AppSettings.h>
#include <ui/Format.h>
#include <ui/LibraryActions.h>
#include <ui/UiLogging.h>

#include <algorithm>

namespace linernotes::ui {

LibraryActions::LibraryActions(
    library::Database &db, player::Player &player, core::Settings &settings, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_player(player)
    , m_settings(settings)
{
}

void LibraryActions::playTracks(
    const QList<qint64> &trackIds, int startIndex, core::PlaySource source)
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
            .playSource = source,
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
            .playSource = core::PlaySource::Queue,
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
            .playSource = core::PlaySource::Queue,
        });
    }

    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return;
    }

    queue->append(items);
}

void LibraryActions::openFiles(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }

    QStringList validPaths;
    validPaths.reserve(paths.size());
    for (const auto &rawPath : paths) {
        const QFileInfo fi(rawPath);
        if (!fi.exists()) {
            qCWarning(lcUi, "openFiles: path does not exist: %s", qPrintable(rawPath));
            continue;
        }
        if (fi.isDir()) {
            qCWarning(lcUi, "openFiles: directory ignored: %s", qPrintable(rawPath));
            continue;
        }
        validPaths.append(fi.absoluteFilePath());
    }

    if (validPaths.isEmpty()) {
        return;
    }

    QHash<QString, qint64> trackIdMap;
    if (m_db.isOpen()) {
        const auto connOpt = m_db.connection();
        if (connOpt.ok()) {
            const library::LibraryQuery query(connOpt.value());
            const auto res = query.trackIdsByPaths(validPaths);
            if (res.ok()) {
                trackIdMap = res.value();
            } else {
                qCWarning(lcUi, "openFiles: failed to query track IDs: %s",
                    qPrintable(res.error().toString()));
            }
        }
    }

    QList<player::QueueItem> items;
    items.reserve(validPaths.size());
    for (const auto &path : validPaths) {
        const qint64 trackId = trackIdMap.value(path, -1);
        items.append(player::QueueItem {
            .source = path,
            .trackId = trackId,
            .playSource = core::PlaySource::External,
        });
    }

    auto *queue = m_player.queue();
    if (queue == nullptr) {
        return;
    }

    queue->setItems(items, 0);
    m_player.playIndex(0);
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
    map.insert(QStringLiteral("favorite"), row.favorite);
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

    const auto pref
        = library::artistNamePreferenceFromString(m_settings.value(kLibraryArtistNamePreference))
              .value_or(library::ArtistNamePreference::Original);

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.artist(artistId, pref);
    if (!res.ok() || !res.value().has_value()) {
        return { };
    }

    const auto &row = res.value().value();
    QVariantMap map;
    map.insert(QStringLiteral("artistId"), row.artistId);
    map.insert(QStringLiteral("name"), row.name);
    map.insert(QStringLiteral("originalName"), row.originalName);
    map.insert(QStringLiteral("trackCount"), row.trackCount);
    map.insert(QStringLiteral("albumCount"), row.albumCount);
    map.insert(QStringLiteral("coverHash"), row.coverHash);
    map.insert(QStringLiteral("favorite"), row.favorite);
    return map;
}

QVariantList LibraryActions::otherVersions(qint64 trackId) const
{
    if (trackId <= 0 || !m_db.isOpen()) {
        return { };
    }

    const auto connOpt = m_db.connection();
    if (!connOpt.ok()) {
        qCWarning(lcUi, "Database connection not available for otherVersions");
        return { };
    }

    const library::LibraryQuery query(connOpt.value());
    const auto res = query.otherVersions(trackId);
    if (!res.ok() || res.value().isEmpty()) {
        return { };
    }

    QVariantList list;
    list.reserve(res.value().size());
    for (const auto &row : res.value()) {
        QVariantMap map;
        map.insert(QStringLiteral("trackId"), row.trackId);
        map.insert(QStringLiteral("title"), row.title);
        map.insert(QStringLiteral("artist"), row.artist);
        map.insert(QStringLiteral("album"), row.album);
        map.insert(QStringLiteral("durationText"), formatDuration(row.durationMs));
        map.insert(QStringLiteral("versionType"),
            row.versionType.has_value() ? static_cast<int>(row.versionType.value()) : -1);
        list.append(map);
    }
    return list;
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

} // namespace linernotes::ui
