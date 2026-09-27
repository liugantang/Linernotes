// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <core/Clock.h>

namespace linernotes::test {

class ManualClock final : public core::Clock {
public:
    explicit ManualClock(qint64 initialMs = 0)
        : m_nowMs(initialMs)
    {
    }
    ~ManualClock() override = default;
    Q_DISABLE_COPY_MOVE(ManualClock)

    [[nodiscard]] qint64 nowMs() const override { return m_nowMs; }

    void set(qint64 ms) { m_nowMs = ms; }

    void advance(qint64 ms) { m_nowMs += ms; }

private:
    qint64 m_nowMs = 0;
};

} // namespace linernotes::test
