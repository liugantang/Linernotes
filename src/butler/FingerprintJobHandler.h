// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QThreadPool>

#include <ai/JobHandler.h>
#include <core/Result.h>

#include <functional>
#include <memory>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class FingerprintJobHandler final : public ai::JobHandler {
public:
    FingerprintJobHandler(library::Database &db, const core::Clock &clock);
    ~FingerprintJobHandler() override;
    Q_DISABLE_COPY_MOVE(FingerprintJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.fingerprint"
    [[nodiscard]] int maxInFlight() const override;
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
    QThreadPool m_pool;
};

} // namespace linernotes::butler
