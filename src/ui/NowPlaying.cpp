// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFileInfo>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/NowPlaying.h>

#include <cmath>

namespace linernotes::ui {

NowPlaying::NowPlaying(library::Database &db, player::Player &player, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_player(player)
{
    auto *queue = m_player.queue();
    if (queue != nullptr) {
        connect(queue, &player::PlayQueue::currentIndexChanged, this, &NowPlaying::refresh);
        connect(queue, &player::PlayQueue::modelReset, this, &NowPlaying::refresh);
    }
    refresh();
}

void NowPlaying::refresh()
{
    auto *queue = m_player.queue();
    const auto currentItemOpt = queue != nullptr ? queue->currentItem() : std::nullopt;

    bool newHasTrack = false;
    qint64 newTrackId = -1;
    QString newTitle;
    QString newArtist;
    QString newAlbum;
    qint64 newAlbumId = 0;
    QString newCoverHash;
    double newDurationSeconds = 0.0;
    bool newFavorite = false;
    int newRating = 0;

    if (currentItemOpt.has_value()) {
        const auto &item = currentItemOpt.value();
        newHasTrack = true;
        newTrackId = item.trackId;
        newTitle = QFileInfo(item.source).fileName();

        if (m_db.isOpen() && newTrackId >= 0) {
            const auto connOpt = m_db.connection();
            if (connOpt.ok()) {
                const library::LibraryQuery query(connOpt.value());
                const auto res = query.tracksByIds({ newTrackId });
                if (res.ok() && !res.value().isEmpty()) {
                    const auto &row = res.value().constFirst();
                    if (!row.title.isEmpty()) {
                        newTitle = row.title;
                    }
                    newArtist = row.artist;
                    newAlbum = row.album;
                    newAlbumId = row.albumId.value_or(0);
                    newCoverHash = row.coverHash;
                    newDurationSeconds = static_cast<double>(row.durationMs) / 1000.0;
                    newFavorite = row.favorite;
                    newRating = row.rating;
                }
            }
        }
    }

    const bool changedValues = (m_hasTrack != newHasTrack || m_trackId != newTrackId
        || m_title != newTitle || m_artist != newArtist || m_album != newAlbum
        || m_albumId != newAlbumId || m_coverHash != newCoverHash || m_favorite != newFavorite
        || m_rating != newRating || std::abs(m_durationSeconds - newDurationSeconds) > 1e-4);

    if (changedValues) {
        m_hasTrack = newHasTrack;
        m_trackId = newTrackId;
        m_title = newTitle;
        m_artist = newArtist;
        m_album = newAlbum;
        m_albumId = newAlbumId;
        m_coverHash = newCoverHash;
        m_durationSeconds = newDurationSeconds;
        m_favorite = newFavorite;
        m_rating = newRating;
        emit changed();
    }
}

bool NowPlaying::hasTrack() const
{
    return m_hasTrack;
}

qint64 NowPlaying::trackId() const
{
    return m_trackId;
}

QString NowPlaying::title() const
{
    return m_title;
}

QString NowPlaying::artist() const
{
    return m_artist;
}

QString NowPlaying::album() const
{
    return m_album;
}

qint64 NowPlaying::albumId() const
{
    return m_albumId;
}

QString NowPlaying::coverHash() const
{
    return m_coverHash;
}

double NowPlaying::durationSeconds() const
{
    return m_durationSeconds;
}

bool NowPlaying::favorite() const
{
    return m_favorite;
}

int NowPlaying::rating() const
{
    return m_rating;
}

} // namespace linernotes::ui
