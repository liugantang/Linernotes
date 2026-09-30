// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <ai/JobHandler.h>

#include <functional>
#include <memory>

namespace linernotes::core {
class Clock;
}

namespace linernotes::ai {
class LlmService;
class PromptLibrary;
} // namespace linernotes::ai

namespace linernotes::library {
class Database;
}

namespace linernotes::butler {

class AlbumInfoJobHandler final : public ai::JobHandler {
public:
    AlbumInfoJobHandler(library::Database &db, ai::LlmService &llm,
        const ai::PromptLibrary &prompts, const core::Clock &clock);
    ~AlbumInfoJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(AlbumInfoJobHandler)

    [[nodiscard]] QString kind() const override;
    [[nodiscard]] int maxInFlight() const override;
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    ai::LlmService &m_llm;
    const ai::PromptLibrary &m_prompts;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
