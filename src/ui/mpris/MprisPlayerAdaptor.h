// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace linernotes::library {
class CoverStore;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class Mpris;
class NowPlaying;

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MprisPlayerAdaptor)
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    MprisPlayerAdaptor(Mpris *parent, player::Player &player, NowPlaying &nowPlaying,
        const library::CoverStore &covers);
    ~MprisPlayerAdaptor() override = default;

    [[nodiscard]] QString playbackStatus() const;
    [[nodiscard]] QString loopStatus() const;
    void setLoopStatus(const QString &loopStatus);

    [[nodiscard]] double rate() const;
    void setRate(double rate);

    [[nodiscard]] bool shuffle() const;
    void setShuffle(bool shuffle);

    [[nodiscard]] QVariantMap metadata() const;

    [[nodiscard]] double volume() const;
    void setVolume(double volume);

    [[nodiscard]] qlonglong position() const;
    [[nodiscard]] double minimumRate() const;
    [[nodiscard]] double maximumRate() const;
    [[nodiscard]] bool canGoNext() const;
    [[nodiscard]] bool canGoPrevious() const;
    [[nodiscard]] bool canPlay() const;
    [[nodiscard]] bool canPause() const;
    [[nodiscard]] bool canSeek() const;
    [[nodiscard]] bool canControl() const;

public slots:
    // NOLINTBEGIN(readability-identifier-naming) - D-Bus method names are fixed by the MPRIS spec
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
    void OpenUri(const QString &uri);
    // NOLINTEND(readability-identifier-naming)

signals:
    // NOLINTBEGIN(readability-identifier-naming) - D-Bus method names are fixed by the MPRIS spec
    void Seeked(qlonglong positionUs);
    // NOLINTEND(readability-identifier-naming)

private:
    [[nodiscard]] bool hasItems() const;
    [[nodiscard]] QDBusObjectPath currentTrackObjectPath() const;
    void sendPropertiesChanged(const QVariantMap &changedProperties);

    player::Player &m_player;
    NowPlaying &m_nowPlaying;
    const library::CoverStore &m_covers;
};

} // namespace linernotes::ui
