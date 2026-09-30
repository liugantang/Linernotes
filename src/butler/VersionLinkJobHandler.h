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

namespace linernotes::ai {
class PromptLibrary;
}

namespace linernotes::butler {

class VersionLinkJobHandler final : public ai::JobHandler {
public:
    VersionLinkJobHandler(
        library::Database &db, const ai::PromptLibrary &prompts, const core::Clock &clock);
    ~VersionLinkJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(VersionLinkJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.version_link"
    [[nodiscard]] int maxInFlight() const override; // 1
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    const ai::PromptLibrary &m_prompts;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
