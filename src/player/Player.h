// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <player/MpvHandle.h>

#include <cstdint>

namespace linernotes::player {

class Player : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Player)

    Q_PROPERTY(PlaybackState state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(QString currentSource READ currentSource NOTIFY currentSourceChanged)

public:
    enum class PlaybackState : std::uint8_t { Stopped, Playing, Paused };
    Q_ENUM(PlaybackState)

    /// extraOptions 透传给 MpvHandle（测试中传 {{"ao","null"}}）
    explicit Player(const MpvHandle::OptionList &extraOptions = { }, QObject *parent = nullptr);
    ~Player() override = default;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] PlaybackState state() const;
    [[nodiscard]] double position() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] int volume() const;
    [[nodiscard]] bool isMuted() const;
    [[nodiscard]] QString currentSource() const;

public slots:
    /// 过渡接口：替换当前播放并立即开始播放（1.5 引入队列后由队列驱动，此方法保留作为底层能力）。
    void openFile(const QString &path);
    void play();
    void pause();
    void togglePause();
    void stop();
    void seek(double seconds);
    void setVolume(int volume);
    void setMuted(bool muted);

signals:
    void stateChanged(linernotes::player::Player::PlaybackState state);
    void positionChanged(double position);
    void durationChanged(double duration);
    void volumeChanged(int volume);
    void mutedChanged(bool muted);
    void currentSourceChanged(const QString &source);
    /// 当前文件正常播放到结尾（mpv end-file reason == Eof）
    void playbackFinished();

private:
    void onPropertyChanged(const QString &name, const QVariant &value);
    void handleIdleActiveChanged(const QVariant &value);
    void handlePauseChanged(const QVariant &value);
    void handleTimePosChanged(const QVariant &value);
    void handleDurationChanged(const QVariant &value);
    void handleVolumeChanged(const QVariant &value);
    void handleMuteChanged(const QVariant &value);
    void updatePlaybackState();

    MpvHandle *m_mpv = nullptr;
    PlaybackState m_state = PlaybackState::Stopped;
    double m_position = 0.0;
    double m_lastEmittedPosition = 0.0;
    double m_duration = 0.0;
    int m_volume = 100;
    bool m_muted = false;
    QString m_currentSource;
    bool m_idleActive = true;
    bool m_pause = false;
};

} // namespace linernotes::player
