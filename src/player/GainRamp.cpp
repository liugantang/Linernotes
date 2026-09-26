// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "GainRamp.h"

#include <algorithm>

namespace linernotes::player {

GainRamp::GainRamp(double from, double to, int durationMs)
    : m_from(std::clamp(from, 0.0, 1.0))
    , m_to(std::clamp(to, 0.0, 1.0))
    , m_durationMs(std::max(0, durationMs))
{
}

double GainRamp::valueAt(qint64 elapsedMs) const
{
    if (m_durationMs <= 0 || elapsedMs >= m_durationMs) {
        return m_to;
    }
    if (elapsedMs <= 0) {
        return m_from;
    }

    const double t = static_cast<double>(elapsedMs) / static_cast<double>(m_durationMs);
    // Smoothstep: S(t) = t * t * (3 - 2 * t)
    const double smoothT = (t * t) * (3.0 - (2.0 * t));
    const double value = m_from + ((m_to - m_from) * smoothT);
    return std::clamp(value, 0.0, 1.0);
}

bool GainRamp::isFinishedAt(qint64 elapsedMs) const
{
    if (m_durationMs <= 0) {
        return true;
    }
    return elapsedMs >= m_durationMs;
}

double GainRamp::target() const
{
    return m_to;
}

} // namespace linernotes::player
