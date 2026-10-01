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
class LlmService;
class PromptLibrary;
} // namespace linernotes::ai

namespace linernotes::butler {

class TitleMatchJobHandler final : public ai::JobHandler {
public:
    TitleMatchJobHandler(library::Database &db, ai::LlmService &llm,
        const ai::PromptLibrary &prompts, const core::Clock &clock);
    ~TitleMatchJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(TitleMatchJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.title_match"
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
