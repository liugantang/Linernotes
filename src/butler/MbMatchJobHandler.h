// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <ai/JobHandler.h>
#include <core/Clock.h>
#include <core/Result.h>

#include <functional>
#include <memory>

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class MusicBrainzClient;

class MbMatchJobHandler final : public ai::JobHandler {
public:
    MbMatchJobHandler(library::Database &db, MusicBrainzClient &mb, const core::Clock &clock);
    ~MbMatchJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(MbMatchJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.mb_match"
    [[nodiscard]] int maxInFlight() const override;
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    MusicBrainzClient &m_mb;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
