// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDateTime>
#include <QtGlobal>

namespace linernotes::core {

class Clock {
public:
    Clock() = default;
    virtual ~Clock() = default;
    Q_DISABLE_COPY_MOVE(Clock)

    [[nodiscard]] virtual qint64 nowMs() const = 0; // Unix epoch 毫秒
};

class SystemClock final : public Clock {
public:
    SystemClock() = default;
    ~SystemClock() override = default;
    Q_DISABLE_COPY_MOVE(SystemClock)

    [[nodiscard]] qint64 nowMs() const override { return QDateTime::currentMSecsSinceEpoch(); }
};

} // namespace linernotes::core
