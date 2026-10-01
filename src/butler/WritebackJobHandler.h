// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QThreadPool>

#include <ai/JobHandler.h>
#include <core/Result.h>

#include <functional>
#include <memory>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class WritebackJobHandler final : public ai::JobHandler {
public:
    explicit WritebackJobHandler(library::Database &db);
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
    QThreadPool m_pool;
};

class WritebackRevertJobHandler final : public ai::JobHandler {
public:
    explicit WritebackRevertJobHandler(library::Database &db);
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
    QThreadPool m_pool;
};

} // namespace linernotes::butler
