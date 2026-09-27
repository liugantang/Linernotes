// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace linernotes::player {

class MpvHandle;

/// 负责音频输出设备枚举、切换及独占模式设置。
class AudioOutput : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AudioOutput)

public:
    explicit AudioOutput(MpvHandle &mpv);
    ~AudioOutput() override = default;

    [[nodiscard]] QVariantList audioDevices() const;
    [[nodiscard]] QString audioDevice() const;
    [[nodiscard]] bool exclusiveMode() const;
    [[nodiscard]] bool isDevicesRefreshed() const;

    /// 刷新并获取音频设备列表。
    /// 首次调用开始观察 `audio-device-list` 并读取当前设备；再次调用重新读取。
    void refreshAudioDevices();

    /// 切换输出设备。name 不在当前 audioDevices 列表中时返回 false 且不做改变。
    bool selectAudioDevice(const QString &name);

    /// 设置音频独占模式。
    void setExclusiveMode(bool exclusive);

signals:
    void audioDevicesChanged();
    void audioDeviceChanged(const QString &name);
    void exclusiveModeChanged(bool exclusive);

private slots:
    void onPropertyChanged(const QString &name, const QVariant &value);

private:
    void handleAudioDeviceListChanged(const QVariant &value);
    void handleAudioDeviceChanged(const QVariant &value);
    void handleAudioExclusiveChanged(const QVariant &value);

    MpvHandle &m_mpv;
    QVariantList m_audioDevices;
    QString m_audioDevice = QStringLiteral("auto");
    bool m_audioDevicesRefreshed = false;
    bool m_exclusiveMode = false;
};

} // namespace linernotes::player
