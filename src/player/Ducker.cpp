// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Ducker.h"

#include "MpvHandle.h"
#include "PlayerLogging.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace linernotes::player {

Ducker::Ducker(MpvHandle &mpv, QString userAudioFilters)
    : m_mpv(mpv)
    , m_userAudioFilters(std::move(userAudioFilters))
{
    m_duckTimer.setInterval(10);
    m_duckTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_duckTimer, &QTimer::timeout, this, &Ducker::onTimerTick);
    connect(&m_mpv, &MpvHandle::audioReconfigured, this, &Ducker::onAudioReconfigured);
}

double Ducker::duckGain() const
{
    return m_duckGain;
}

int Ducker::duckApplyCount() const
{
    return m_duckApplyCount;
}

QString Ducker::formattedDuckFilter(double gain) const
{
    QString duckFilter = QStringLiteral("@duck:lavfi=[volume=volume=")
        + QString::number(gain, 'f', 6) + QStringLiteral("]");
    if (m_userAudioFilters.isEmpty()) {
        return duckFilter;
    }
    return m_userAudioFilters + QStringLiteral(",") + duckFilter;
}

void Ducker::duckTo(double gain, int rampMs)
{
    const double targetGain = std::clamp(gain, 0.0, 1.0);
    qCDebug(lcPlayer) << "duckTo target:" << targetGain << "rampMs:" << rampMs
                      << "current duckGain:" << m_duckGain;

    if (rampMs <= 0) {
        m_duckTimer.stop();
        m_duckRamp = GainRamp(targetGain, targetGain, 0);
        m_duckGain = targetGain;
        applyDuckGainToMpv();
        if (std::abs(m_duckGain - m_lastEmittedDuckGain) > 1e-6) {
            m_lastEmittedDuckGain = m_duckGain;
            emit duckGainChanged(m_duckGain);
        }
        emit duckFinished(m_duckGain);
        return;
    }

    if (std::abs(m_duckGain - targetGain) < 1e-6 && !m_duckTimer.isActive()) {
        emit duckFinished(m_duckGain);
        return;
    }

    m_duckRamp = GainRamp(m_duckGain, targetGain, rampMs);
    m_duckElapsedTimer.restart();
    if (!m_duckTimer.isActive()) {
        m_duckTimer.start();
    }
}

void Ducker::unduck(int rampMs)
{
    duckTo(1.0, rampMs);
}

void Ducker::onTimerTick()
{
    const qint64 elapsed = m_duckElapsedTimer.elapsed();
    const double newGain = m_duckRamp.valueAt(elapsed);
    m_duckGain = newGain;
    applyDuckGainToMpv();

    if (std::abs(m_duckGain - m_lastEmittedDuckGain) >= 0.02) {
        m_lastEmittedDuckGain = m_duckGain;
        emit duckGainChanged(m_duckGain);
    }

    if (m_duckRamp.isFinishedAt(elapsed)) {
        m_duckTimer.stop();
        m_duckGain = m_duckRamp.target();
        applyDuckGainToMpv();
        if (std::abs(m_duckGain - m_lastEmittedDuckGain) > 1e-6) {
            m_lastEmittedDuckGain = m_duckGain;
            emit duckGainChanged(m_duckGain);
        }
        emit duckFinished(m_duckGain);
    }
}

void Ducker::onAudioReconfigured()
{
    qCDebug(lcPlayer) << "audioReconfigured, re-applying duck gain:" << m_duckGain;
    applyDuckGainToMpv();
}

void Ducker::applyDuckGainToMpv()
{
    if (!m_mpv.isValid()) {
        return;
    }
    const bool ok = m_mpv.command(
        { QStringLiteral("af-command"), QStringLiteral("duck"), QStringLiteral("volume"),
            QString::number(m_duckGain, 'f', 6), QStringLiteral("volume") },
        false);
    if (ok) {
        ++m_duckApplyCount;
    }
}

} // namespace linernotes::player
