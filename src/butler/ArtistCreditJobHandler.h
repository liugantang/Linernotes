// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>

#include <ai/JobHandler.h>
#include <butler/ArtistCredit.h>
#include <core/Result.h>

#include <functional>
#include <memory>
#include <optional>

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

class ArtistCreditJobHandler final : public ai::JobHandler {
public:
    ArtistCreditJobHandler(library::Database &db, ai::LlmService &llm,
        const ai::PromptLibrary &prompts, const core::Clock &clock);
    ~ArtistCreditJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(ArtistCreditJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.artist_credit"
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    core::Result<void> addCreditProposals(const QHash<QString, ArtistCredit> &credits,
        qint64 batchId, std::optional<double> autoAcceptThreshold) const;

    std::unique_ptr<QObject> startLlm(const QStringList &values, qint64 batchId,
        std::optional<double> autoAcceptThreshold,
        std::function<void(const core::Result<void> &)> done);

    library::Database &m_db;
    ai::LlmService &m_llm;
    const ai::PromptLibrary &m_prompts;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
