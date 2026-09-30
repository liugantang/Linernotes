// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

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

class DuplicateJobHandler final : public ai::JobHandler {
public:
    DuplicateJobHandler(library::Database &db, const core::Clock &clock);
    ~DuplicateJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(DuplicateJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.duplicates"
    [[nodiscard]] int maxInFlight() const override; // 1
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
