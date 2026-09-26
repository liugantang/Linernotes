// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <player/GainRamp.h>

namespace linernotes::player {

class MpvHandle;

/// 负责音量闪避（Ducking）与音频滤镜链管理。
///
/// 持有 MpvHandle 引用，通过定时器执行增益渐变，并通过 af-command 向 mpv 注入 lavfi volume 滤镜。
/// 在滤镜链重构（audioReconfigured）时自动重新下发当前增益。
class Ducker : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Ducker)

public:
    explicit Ducker(MpvHandle &mpv, QString userAudioFilters = { });
    ~Ducker() override = default;

    [[nodiscard]] double duckGain() const;
    [[nodiscard]] int duckApplyCount() const;
    [[nodiscard]] QString formattedDuckFilter(double gain) const;

public slots:
    /// 在 rampMs 内平滑渐变到 gain（夹到 [0,
    /// 1]）。渐变进行中再次调用：从当前值开始新的渐变，不跳变。
    void duckTo(double gain, int rampMs = 300);
    /// 等价于 duckTo(1.0, rampMs)
    void unduck(int rampMs = 500);

signals:
    void duckGainChanged(double gain);
    void duckFinished(double gain);

private slots:
    void onTimerTick();
    void onAudioReconfigured();

private:
    void applyDuckGainToMpv();

    MpvHandle &m_mpv;
    QString m_userAudioFilters;
    double m_duckGain = 1.0;
    double m_lastEmittedDuckGain = 1.0;
    int m_duckApplyCount = 0;
    GainRamp m_duckRamp;
    QTimer m_duckTimer;
    QElapsedTimer m_duckElapsedTimer;
};

} // namespace linernotes::player
