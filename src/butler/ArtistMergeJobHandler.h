// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <ai/JobHandler.h>
#include <butler/ArtistMergeLlm.h>
#include <butler/ArtistMergeSource.h>
#include <butler/MusicBrainzClient.h>

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

class ArtistMergeJobHandler final : public ai::JobHandler {
public:
    ArtistMergeJobHandler(library::Database &db, ai::LlmService &llm,
        const ai::PromptLibrary &prompts, MusicBrainzClient &mbClient, const core::Clock &clock);
    ~ArtistMergeJobHandler() override = default;
    Q_DISABLE_COPY_MOVE(ArtistMergeJobHandler)

    [[nodiscard]] QString kind() const override; // "butler.artist_merge"
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    std::unique_ptr<QObject> processGroup(const QJsonObject &keyObj, qint64 batchId,
        std::optional<double> autoAcceptThreshold,
        const std::function<void(const core::Result<void> &)> &done);

    std::unique_ptr<QObject> processConfirm(const QJsonObject &keyObj, const QJsonObject &params,
        qint64 batchId, std::optional<double> autoAcceptThreshold,
        std::function<void(const core::Result<void> &)> done);

    std::unique_ptr<QObject> processMb(const QJsonObject &keyObj, qint64 batchId,
        std::optional<double> autoAcceptThreshold,
        std::function<void(const core::Result<void> &)> done);

    std::unique_ptr<QObject> startLlm(const QList<ArtistMergeGroup> &groups, qint64 batchId,
        std::optional<double> autoAcceptThreshold,
        std::function<void(const core::Result<void> &)> done);

    library::Database &m_db;
    ai::LlmService &m_llm;
    const ai::PromptLibrary &m_prompts;
    MusicBrainzClient &m_mbClient;
    const core::Clock &m_clock;
};

} // namespace linernotes::butler
