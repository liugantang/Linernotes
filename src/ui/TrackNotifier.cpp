// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "TrackNotifier.h"

#include "NotificationSink.h"
#include "NowPlaying.h"
#include "SettingsController.h"

#include <QGuiApplication>

#include <library/CoverStore.h>
#include <player/Player.h>

namespace linernotes::ui {

TrackNotifier::TrackNotifier(NowPlaying &nowPlaying, player::Player &player,
    const library::CoverStore &covers, const SettingsController &settings, NotificationSink &sink,
    QObject *parent)
    : QObject(parent)
    , m_nowPlaying(nowPlaying)
    , m_player(player)
    , m_covers(covers)
    , m_settings(settings)
    , m_sink(sink)
{
    connect(&m_nowPlaying, &NowPlaying::changed, this, &TrackNotifier::checkAndNotify);
    connect(&m_player, &player::Player::stateChanged, this,
        [this](player::Player::PlaybackState state) {
            if (state == player::Player::PlaybackState::Playing) {
                checkAndNotify();
            }
        });
}

void TrackNotifier::checkAndNotify()
{
    if (!m_settings.trackChangeNotifications()) {
        return;
    }
    if (!m_nowPlaying.hasTrack()) {
        return;
    }
    if (m_player.state() != player::Player::PlaybackState::Playing) {
        return;
    }

    const qint64 trackId = m_nowPlaying.trackId();
    if (trackId == m_lastTrackId) {
        return;
    }

    m_lastTrackId = trackId;

    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        return;
    }

    const QString title = m_nowPlaying.title();
    const QString artist = m_nowPlaying.artist();
    const QString album = m_nowPlaying.album();

    QString body;
    if (!artist.isEmpty() && !album.isEmpty()) {
        body = artist + QStringLiteral(" — ") + album;
    } else if (!artist.isEmpty()) {
        body = artist;
    } else if (!album.isEmpty()) {
        body = album;
    }

    const QString iconPath = m_covers.thumbnailPath(m_nowPlaying.coverHash(), 256);
    m_sink.show(title, body, iconPath);
}

} // namespace linernotes::ui
