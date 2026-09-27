// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MprisPlayerAdaptor.h"

#include "Mpris.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QUrl>

#include <library/CoverStore.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/NowPlaying.h>

#include <algorithm>
#include <cmath>

namespace linernotes::ui {

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris *parent, player::Player &player,
    NowPlaying &nowPlaying, const library::CoverStore &covers)
    : QDBusAbstractAdaptor(parent)
    , m_player(player)
    , m_nowPlaying(nowPlaying)
    , m_covers(covers)
{
    connect(&m_player, &player::Player::seeked, this,
        [this](double posSec) { emit Seeked(static_cast<qlonglong>(posSec * 1000000.0)); });

    connect(&m_player, &player::Player::stateChanged, this, [this]() {
        QVariantMap changed;
        changed.insert(QStringLiteral("PlaybackStatus"), playbackStatus());
        sendPropertiesChanged(changed);
    });

    connect(&m_player, &player::Player::volumeChanged, this, [this]() {
        QVariantMap changed;
        changed.insert(QStringLiteral("Volume"), volume());
        sendPropertiesChanged(changed);
    });

    auto *queue = m_player.queue();
    connect(queue, &player::PlayQueue::modeChanged, this, [this]() {
        QVariantMap changed;
        changed.insert(QStringLiteral("LoopStatus"), loopStatus());
        changed.insert(QStringLiteral("Shuffle"), shuffle());
        sendPropertiesChanged(changed);
    });

    connect(queue, &player::PlayQueue::countChanged, this, [this]() {
        QVariantMap changed;
        changed.insert(QStringLiteral("CanGoNext"), canGoNext());
        changed.insert(QStringLiteral("CanGoPrevious"), canGoPrevious());
        changed.insert(QStringLiteral("CanPlay"), canPlay());
        changed.insert(QStringLiteral("CanPause"), canPause());
        changed.insert(QStringLiteral("CanSeek"), canSeek());
        sendPropertiesChanged(changed);
    });

    connect(&m_nowPlaying, &NowPlaying::changed, this, [this]() {
        QVariantMap changed;
        changed.insert(QStringLiteral("Metadata"), metadata());
        sendPropertiesChanged(changed);
    });

    connect(&m_player, &player::Player::currentSourceChanged, this, [this]() {
        QVariantMap changed;
        changed.insert(QStringLiteral("Metadata"), metadata());
        sendPropertiesChanged(changed);
    });
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    switch (m_player.state()) {
    case player::Player::PlaybackState::Playing:
        return QStringLiteral("Playing");
    case player::Player::PlaybackState::Paused:
        return QStringLiteral("Paused");
    case player::Player::PlaybackState::Stopped:
    default:
        return QStringLiteral("Stopped");
    }
}

QString MprisPlayerAdaptor::loopStatus() const
{
    switch (m_player.queue()->mode()) {
    case player::PlayMode::RepeatOne:
        return QStringLiteral("Track");
    case player::PlayMode::RepeatAll:
        return QStringLiteral("Playlist");
    case player::PlayMode::Sequential:
    case player::PlayMode::Shuffle:
    default:
        return QStringLiteral("None");
    }
}

void MprisPlayerAdaptor::setLoopStatus(const QString &loopStatus)
{
    auto *queue = m_player.queue();
    if (loopStatus == QLatin1String("Track")) {
        queue->setMode(player::PlayMode::RepeatOne);
    } else if (loopStatus == QLatin1String("Playlist")) {
        queue->setMode(player::PlayMode::RepeatAll);
    } else if (loopStatus == QLatin1String("None")) {
        queue->setMode(player::PlayMode::Sequential);
    }
}

double MprisPlayerAdaptor::rate() const
{
    return 1.0;
}

void MprisPlayerAdaptor::setRate(double rate)
{
    Q_UNUSED(rate);
}

bool MprisPlayerAdaptor::shuffle() const
{
    return m_player.queue()->mode() == player::PlayMode::Shuffle;
}

void MprisPlayerAdaptor::setShuffle(bool shuffle)
{
    auto *queue = m_player.queue();
    if (shuffle) {
        queue->setMode(player::PlayMode::Shuffle);
    } else if (queue->mode() == player::PlayMode::Shuffle) {
        queue->setMode(player::PlayMode::Sequential);
    }
}

QDBusObjectPath MprisPlayerAdaptor::currentTrackObjectPath() const
{
    if (m_nowPlaying.hasTrack()) {
        if (m_nowPlaying.trackId() >= 0) {
            return QDBusObjectPath(
                QStringLiteral("/org/linernotes/track/%1").arg(m_nowPlaying.trackId()));
        }
        return QDBusObjectPath(QStringLiteral("/org/linernotes/track/external"));
    }
    if (!m_player.currentSource().isEmpty()) {
        return QDBusObjectPath(QStringLiteral("/org/linernotes/track/external"));
    }
    return QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    QVariantMap map;
    const QDBusObjectPath trackPath = currentTrackObjectPath();
    map.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath));

    if (trackPath.path() == QLatin1String("/org/mpris/MediaPlayer2/TrackList/NoTrack")) {
        return map;
    }

    double durSec = m_nowPlaying.durationSeconds();
    if (durSec <= 0.0) {
        durSec = m_player.duration();
    }
    if (durSec > 0.0) {
        const auto lengthUs = static_cast<qlonglong>(durSec * 1000000.0);
        map.insert(QStringLiteral("mpris:length"), lengthUs);
    }

    QString title = m_nowPlaying.title();
    if (title.isEmpty() && !m_player.currentSource().isEmpty()) {
        title = QFileInfo(m_player.currentSource()).fileName();
    }
    if (!title.isEmpty()) {
        map.insert(QStringLiteral("xesam:title"), title);
    }

    if (!m_nowPlaying.artist().isEmpty()) {
        map.insert(QStringLiteral("xesam:artist"), QStringList { m_nowPlaying.artist() });
    }

    if (!m_nowPlaying.album().isEmpty()) {
        map.insert(QStringLiteral("xesam:album"), m_nowPlaying.album());
    }

    if (!m_player.currentSource().isEmpty()) {
        map.insert(
            QStringLiteral("xesam:url"), QUrl::fromLocalFile(m_player.currentSource()).toString());
    }

    if (!m_nowPlaying.coverHash().isEmpty()) {
        const QString thumbPath = m_covers.thumbnailPath(m_nowPlaying.coverHash(), 512);
        if (!thumbPath.isEmpty()) {
            map.insert(QStringLiteral("mpris:artUrl"), QUrl::fromLocalFile(thumbPath).toString());
        }
    }

    return map;
}

double MprisPlayerAdaptor::volume() const
{
    return static_cast<double>(m_player.volume()) / 100.0;
}

void MprisPlayerAdaptor::setVolume(double volume)
{
    const int vol = std::clamp(static_cast<int>(std::round(volume * 100.0)), 0, 100);
    m_player.setVolume(vol);
}

qlonglong MprisPlayerAdaptor::position() const
{
    return static_cast<qlonglong>(m_player.position() * 1000000.0);
}

double MprisPlayerAdaptor::minimumRate() const
{
    return 1.0;
}

double MprisPlayerAdaptor::maximumRate() const
{
    return 1.0;
}

bool MprisPlayerAdaptor::hasItems() const
{
    return m_player.queue()->count() > 0;
}

bool MprisPlayerAdaptor::canGoNext() const
{
    return hasItems();
}

bool MprisPlayerAdaptor::canGoPrevious() const
{
    return hasItems();
}

bool MprisPlayerAdaptor::canPlay() const
{
    return hasItems();
}

bool MprisPlayerAdaptor::canPause() const
{
    return hasItems();
}

bool MprisPlayerAdaptor::canSeek() const
{
    return hasItems();
}

bool MprisPlayerAdaptor::canControl() const
{
    return true;
}

void MprisPlayerAdaptor::Next()
{
    m_player.next();
}

void MprisPlayerAdaptor::Previous()
{
    m_player.previous();
}

void MprisPlayerAdaptor::Pause()
{
    m_player.pause();
}

void MprisPlayerAdaptor::PlayPause()
{
    m_player.togglePause();
}

void MprisPlayerAdaptor::Stop()
{
    m_player.stop();
}

void MprisPlayerAdaptor::Play()
{
    m_player.play();
}

void MprisPlayerAdaptor::Seek(qlonglong offsetUs)
{
    const double curPos = m_player.position();
    const double offsetSec = static_cast<double>(offsetUs) / 1000000.0;
    const double targetPos = std::max(0.0, curPos + offsetSec);
    const double duration = m_player.duration();
    if (duration > 0.0 && targetPos > duration) {
        m_player.next();
    } else {
        m_player.seek(targetPos);
    }
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    if (positionUs < 0) {
        return;
    }
    const QDBusObjectPath currentPath = currentTrackObjectPath();
    if (currentPath.path() == QLatin1String("/org/mpris/MediaPlayer2/TrackList/NoTrack")) {
        return;
    }
    if (trackId.path() != currentPath.path()) {
        return;
    }
    const double targetSec = static_cast<double>(positionUs) / 1000000.0;
    const double duration = m_player.duration();
    if (duration > 0.0 && targetSec > duration) {
        return;
    }
    m_player.seek(targetSec);
}

void MprisPlayerAdaptor::OpenUri(const QString &uri)
{
    const QUrl url(uri);
    if (url.isLocalFile()) {
        m_player.openFile(url.toLocalFile());
    }
}

void MprisPlayerAdaptor::sendPropertiesChanged(const QVariantMap &changedProperties)
{
    if (changedProperties.isEmpty()) {
        return;
    }
    if (!QDBusConnection::sessionBus().isConnected()) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    msg << QStringLiteral("org.mpris.MediaPlayer2.Player");
    msg << changedProperties;
    msg << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

} // namespace linernotes::ui
