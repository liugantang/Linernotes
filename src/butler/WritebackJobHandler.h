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

class WritebackJobHandler final : public ai::JobHandler {
public:
    WritebackJobHandler(library::Database &db, const core::Clock &clock);
    ~WritebackJobHandler() override;
    Q_DISABLE_COPY_MOVE(WritebackJobHandler)

    [[nodiscard]] QString kind() const override; // "library.writeback"
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

class WritebackRevertJobHandler final : public ai::JobHandler {
public:
    WritebackRevertJobHandler(library::Database &db, const core::Clock &clock);
    ~WritebackRevertJobHandler() override;
    Q_DISABLE_COPY_MOVE(WritebackRevertJobHandler)

    [[nodiscard]] QString kind() const override; // "library.writeback_revert"
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
