// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantList>

#include <player/GainRamp.h>
#include <player/MpvHandle.h>
#include <player/PlaybackSnapshot.h>

#include <cstdint>

namespace linernotes::player {

class PlayQueue;
struct QueueItem;

class Player : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Player)

    Q_PROPERTY(linernotes::player::PlayQueue *queue READ queue CONSTANT)
    Q_PROPERTY(PlaybackState state READ state NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ isMuted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double duckGain READ duckGain NOTIFY duckGainChanged)
    Q_PROPERTY(QString currentSource READ currentSource NOTIFY currentSourceChanged)
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice NOTIFY audioDeviceChanged)
    Q_PROPERTY(
        bool exclusiveMode READ exclusiveMode WRITE setExclusiveMode NOTIFY exclusiveModeChanged)

public:
    enum class PlaybackState : std::uint8_t { Stopped, Playing, Paused };
    Q_ENUM(PlaybackState)

    /// extraOptions 透传给 MpvHandle（测试中传 {{"ao","null"}}）
    explicit Player(const MpvHandle::OptionList &extraOptions = { }, QObject *parent = nullptr);
    ~Player() override = default;

    [[nodiscard]] PlayQueue *queue() const;
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] PlaybackState state() const;
    [[nodiscard]] double position() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] int volume() const;
    [[nodiscard]] bool isMuted() const;
    [[nodiscard]] double duckGain() const;
    [[nodiscard]] QString currentSource() const;
    [[nodiscard]] QVariantList audioDevices() const;
    [[nodiscard]] QString audioDevice() const;
    [[nodiscard]] bool exclusiveMode() const;

    [[nodiscard]] PlaybackSnapshot snapshot() const;
    /// 用快照替换当前队列与设置；若 currentIndex 有效，则加载该项、定位到 position 并保持暂停
    /// （不能先出声再暂停：加载前就设为暂停，定位可用 loadfile 的 start 选项或 fileLoaded 后
    /// seek）。
    void restore(const PlaybackSnapshot &snapshot);

    /// 仅供测试与调试：返回 mpv 内部播放列表当前的项数（不变式：≤ 2）
    [[nodiscard]] int mpvPlaylistCount() const;

    /// 仅供测试与调试：返回 mpv 当前的 af 属性值
    [[nodiscard]] QVariant mpvAudioFilters() const;

    /// 仅供测试与调试：返回成功执行 af-command 的次数
    [[nodiscard]] int duckApplyCount() const;

public slots:
    void openFile(const QString &path);
    void playIndex(int row);
    void next();
    void previous();
    void play();
    void pause();
    void togglePause();
    void stop();
    void seek(double seconds);
    void setVolume(int volume);
    void setMuted(bool muted);
    /// 在 rampMs 内平滑渐变到 gain（夹到
    /// [0,1]）。渐变进行中再次调用：从当前值开始新的渐变，不跳变。
    void duckTo(double gain, int rampMs = 300);
    /// 等价于 duckTo(1.0, rampMs)
    void unduck(int rampMs = 500);
    /// 刷新并获取音频设备列表。
    ///
    /// 首次调用时开始观察 `audio-device-list` 并读取当前设备列表（此后热插拔自动更新）；
    /// 再次调用重新读取设备列表。UI 在用户打开设备设置时调用。
    /// 为什么不在构造时枚举：在无音频服务环境（如 CI 容器）中，libpipewire 在构造时启动设备
    /// 热插拔监听可能在销毁时发生死锁；此外按需枚举可避免应用启动时的无谓开销。
    Q_INVOKABLE void refreshAudioDevices();
    /// 切换输出设备。name 不在当前 audioDevices 列表中时返回 false 且不做任何改变（记 qCWarning）。
    bool selectAudioDevice(const QString &name);
    void setExclusiveMode(bool exclusive);

signals:
    void stateChanged(linernotes::player::Player::PlaybackState state);
    void positionChanged(double position);
    void durationChanged(double duration);
    void volumeChanged(int volume);
    void mutedChanged(bool muted);
    void duckGainChanged(double gain);
    void duckFinished(double gain);
    void currentSourceChanged(const QString &source);
    void audioDevicesChanged();
    void audioDeviceChanged(const QString &name);
    void exclusiveModeChanged(bool exclusive);
    /// 语义：队列播放结束（最后一首自然播完且没有下一首）
    void playbackFinished();
    /// 某项无法播放（文件不存在、格式无法识别/解码失败）。source 为该项路径，message
    /// 为可读原因（来自 mpv）。
    void playbackError(const QString &source, const QString &message);

private slots:
    void onDuckTimerTick();

private:
    void onPropertyChanged(const QString &name, const QVariant &value);
    void handleIdleActiveChanged(const QVariant &value);
    void handlePauseChanged(const QVariant &value);
    void handleTimePosChanged(const QVariant &value);
    void handleDurationChanged(const QVariant &value);
    void handleVolumeChanged(const QVariant &value);
    void handleMuteChanged(const QVariant &value);
    void handleAudioDeviceListChanged(const QVariant &value);
    void handleAudioDeviceChanged(const QVariant &value);
    void handleAudioExclusiveChanged(const QVariant &value);
    void updatePlaybackState();

    void onStartFile(qint64 entryId);
    void onFileLoaded();
    void onAudioReconfigured();
    void onEndFile(qint64 entryId, MpvHandle::EndFileReason reason, const QString &error);
    void handleEndFileError(qint64 entryId, const QString &failedSource, const QString &error);
    void onUpcomingChanged();
    void schedulePreloadSync();
    void syncPreload();
    void loadCurrentItem(const QueueItem &item);
    void loadItemPaused(const QueueItem &item, double startPosition);
    [[nodiscard]] qint64 lastPlaylistEntryId() const;
    void applyDuckGainToMpv();
    [[nodiscard]] QString formattedDuckFilter(double gain) const;

    MpvHandle *m_mpv = nullptr;
    PlayQueue *m_queue = nullptr;
    PlaybackState m_state = PlaybackState::Stopped;
    double m_position = 0.0;
    double m_lastEmittedPosition = 0.0;
    double m_duration = 0.0;
    int m_volume = 100;
    bool m_muted = false;
    double m_duckGain = 1.0;
    double m_lastEmittedDuckGain = 1.0;
    int m_duckApplyCount = 0;
    GainRamp m_duckRamp;
    QTimer m_duckTimer;
    QElapsedTimer m_duckElapsedTimer;
    QString m_currentSource;
    QString m_userAudioFilters;
    QVariantList m_audioDevices;
    QString m_audioDevice = QStringLiteral("auto");
    bool m_audioDevicesRefreshed = false;
    bool m_exclusiveMode = false;
    bool m_idleActive = true;
    bool m_pause = false;

    qint64 m_currentEntryId = -1;
    quint64 m_currentUid = 0;
    qint64 m_preloadEntryId = -1;
    quint64 m_preloadUid = 0;
    bool m_preloadIsRepeatOne = false;
    bool m_preloadSyncPending = false;
    bool m_inInternalSync = false;
    QHash<qint64, QString> m_entrySources;
    QSet<qint64> m_ignoredEntryIds;
    int m_consecutiveErrorCount = 0;
};

} // namespace linernotes::player
