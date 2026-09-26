// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

#include <player/MpvHandle.h>

namespace linernotes::player {

/// 独立语音通道，用于播放串场解说/DJ 语音。
///
/// 内部持有一个独立的 mpv 实例，与主音乐播放通道（Player）互不依赖，
/// 可实现并行播放与独立音量控制。
class VoiceChannel : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(VoiceChannel)

    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged) // 0–100

public:
    explicit VoiceChannel(
        const MpvHandle::OptionList &extraOptions = { }, QObject *parent = nullptr);
    ~VoiceChannel() override = default;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] int volume() const;

public slots:
    void play(const QString &path); // 替换正在播放的语音
    void stop();
    void setVolume(int volume);

signals:
    void playingChanged(bool playing);
    void volumeChanged(int volume);
    void started();
    void finished(); // 语音正常播完
    void failed(const QString &message);

private slots:
    void onPropertyChanged(const QString &name, const QVariant &value);
    void onStartFile(qint64 entryId);
    void onFileLoaded();
    void onEndFile(qint64 entryId, MpvHandle::EndFileReason reason, const QString &error);

private:
    MpvHandle *m_mpv = nullptr;
    bool m_playing = false;
    int m_volume = 100;
    qint64 m_currentEntryId = -1;
    bool m_userStopped = false;
};

} // namespace linernotes::player
