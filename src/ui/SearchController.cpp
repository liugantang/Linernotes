// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "SearchController.h"

#include <QTimer>
#include <QVariantMap>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <ui/Format.h>

#include <utility>

namespace linernotes::ui {

SearchController::SearchController(library::Database &db, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_debounceTimer(new QTimer(this))
{
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &SearchController::performSearch);
}

QString SearchController::query() const
{
    return m_query;
}

void SearchController::setQuery(const QString &q)
{
    if (m_query == q) {
        return;
    }
    m_query = q;
    emit queryChanged();

    if (!isActive()) {
        m_debounceTimer->stop();
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        if (!m_tracks.isEmpty() || !m_albums.isEmpty() || !m_artists.isEmpty()
            || !m_trackIds.isEmpty()) {
            m_tracks.clear();
            m_albums.clear();
            m_artists.clear();
            m_trackIds.clear();
            emit resultsChanged();
        }
    } else {
        if (!m_searching) {
            m_searching = true;
            emit searchingChanged();
        }
        m_debounceTimer->start(120);
    }
}

bool SearchController::isActive() const
{
    return !m_query.trimmed().isEmpty();
}

bool SearchController::isSearching() const
{
    return m_searching;
}

QVariantList SearchController::tracks() const
{
    return m_tracks;
}

QVariantList SearchController::albums() const
{
    return m_albums;
}

QVariantList SearchController::artists() const
{
    return m_artists;
}

QList<qint64> SearchController::trackIds() const
{
    return m_trackIds;
}

void SearchController::clear()
{
    setQuery(QString());
}

void SearchController::refresh()
{
    if (isActive()) {
        performSearch();
    }
}

void SearchController::performSearch()
{
    m_debounceTimer->stop();
    if (!isActive()) {
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        if (!m_tracks.isEmpty() || !m_albums.isEmpty() || !m_artists.isEmpty()
            || !m_trackIds.isEmpty()) {
            m_tracks.clear();
            m_albums.clear();
            m_artists.clear();
            m_trackIds.clear();
            emit resultsChanged();
        }
        return;
    }

    if (!m_db.isOpen()) {
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        return;
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        if (m_searching) {
            m_searching = false;
            emit searchingChanged();
        }
        return;
    }

    const library::LibraryQuery libraryQuery(connRes.value());
    const auto searchRes = libraryQuery.searchGrouped(m_query);

    QVariantList newTracks;
    QVariantList newAlbums;
    QVariantList newArtists;
    QList<qint64> newTrackIds;

    if (searchRes.ok()) {
        const auto &results = searchRes.value();

        newTracks.reserve(results.tracks.size());
        newTrackIds.reserve(results.tracks.size());
        for (const auto &t : results.tracks) {
            newTrackIds.append(t.trackId);
            QVariantMap map;
            map.insert(QStringLiteral("trackId"), t.trackId);
            map.insert(QStringLiteral("title"), t.title);
            map.insert(QStringLiteral("artist"), t.artist);
            map.insert(QStringLiteral("album"), t.album);
            map.insert(QStringLiteral("albumId"), t.albumId.value_or(0));
            map.insert(QStringLiteral("durationText"), formatDuration(t.durationMs));
            map.insert(QStringLiteral("coverHash"), t.coverHash);
            newTracks.append(map);
        }

        newAlbums.reserve(results.albums.size());
        for (const auto &a : results.albums) {
            QVariantMap map;
            map.insert(QStringLiteral("albumId"), a.albumId);
            map.insert(QStringLiteral("title"), a.title);
            map.insert(QStringLiteral("albumArtist"), a.albumArtist);
            map.insert(
                QStringLiteral("year"), a.year.has_value() ? QVariant(a.year.value()) : QVariant());
            map.insert(QStringLiteral("coverHash"), a.coverHash);
            newAlbums.append(map);
        }

        newArtists.reserve(results.artists.size());
        for (const auto &ar : results.artists) {
            QVariantMap map;
            map.insert(QStringLiteral("artistId"), ar.artistId);
            map.insert(QStringLiteral("name"), ar.name);
            map.insert(QStringLiteral("albumCount"), ar.albumCount);
            map.insert(QStringLiteral("trackCount"), ar.trackCount);
            map.insert(QStringLiteral("coverHash"), ar.coverHash);
            newArtists.append(map);
        }
    }

    m_tracks = std::move(newTracks);
    m_albums = std::move(newAlbums);
    m_artists = std::move(newArtists);
    m_trackIds = std::move(newTrackIds);

    if (m_searching) {
        m_searching = false;
        emit searchingChanged();
    }
    emit resultsChanged();
}

} // namespace linernotes::ui
