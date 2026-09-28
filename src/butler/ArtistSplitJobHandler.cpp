// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistSplitJobHandler.h"

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/ArtistSplitLlm.h>
#include <butler/ArtistSplitSource.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

ArtistSplitJobHandler::ArtistSplitJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString ArtistSplitJobHandler::kind() const
{
    return QStringLiteral("butler.artist_split");
}

ai::TokenUsage ArtistSplitJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const ArtistSplitSource source(m_db);
    const auto groupRes = source.loadItem(itemKey);
    if (!groupRes.ok()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    int ambiguousCount = 0;
    for (const auto &cand : groupRes.value().candidates) {
        if (cand.decision.verdict == SplitVerdict::Ambiguous) {
            ++ambiguousCount;
        }
    }
    if (ambiguousCount == 0) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto vars = artistSplitPromptVars(groupRes.value());
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/artist_split"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        promptTokens = (ambiguousCount * 80) + 200;
    }
    const int completionTokens = ambiguousCount * 50;

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> ArtistSplitJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const ArtistSplitSource source(m_db);
    const auto groupRes = source.loadItem(itemKey);
    if (!groupRes.ok()) {
        done(groupRes.error());
        return nullptr;
    }

    const qint64 batchId = params.value(QStringLiteral("batchId")).toInteger(0);
    if (batchId <= 0) {
        done(core::Error {
            .code = QString(errc::kArtistSplitInvalidKey),
            .message = QStringLiteral("Missing or invalid batchId in params"),
            .detail = QString::number(batchId),
        });
        return nullptr;
    }

    std::optional<double> autoAcceptThreshold;
    if (params.contains(QStringLiteral("autoAccept"))
        && !params.value(QStringLiteral("autoAccept")).isNull()) {
        autoAcceptThreshold = params.value(QStringLiteral("autoAccept")).toDouble();
    }

    const auto &group = groupRes.value();
    QList<library::CorrectionProposal> ruleProposals;
    bool hasAmbiguous = false;

    for (const auto &cand : group.candidates) {
        if (cand.decision.verdict == SplitVerdict::Split) {
            const QString newVal = cand.decision.parts.join(QStringLiteral(" / "));
            for (const auto &target : cand.targets) {
                ruleProposals.append(library::CorrectionProposal {
                    .trackId = target.trackId,
                    .field = target.field,
                    .oldValue = cand.original,
                    .newValue = newVal,
                    .source = library::CorrectionSource::Rule,
                    .confidence = cand.decision.confidence,
                    .reason = cand.decision.reason,
                });
            }
        } else if (cand.decision.verdict == SplitVerdict::Ambiguous) {
            hasAmbiguous = true;
        }
    }

    library::CorrectionStore store(m_db, m_clock);
    if (!ruleProposals.isEmpty()) {
        auto addRes = store.addProposals(batchId, ruleProposals, autoAcceptThreshold);
        if (!addRes.ok()) {
            done(addRes);
            return nullptr;
        }
    }

    if (!hasAmbiguous) {
        done({ });
        return nullptr;
    }

    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        done({ });
        return nullptr;
    }

    return startLlm(group, batchId, autoAcceptThreshold, std::move(done));
}

std::unique_ptr<QObject> ArtistSplitJobHandler::startLlm(const ArtistSplitGroup &group,
    qint64 batchId, std::optional<double> autoAcceptThreshold,
    std::function<void(const core::Result<void> &)> done)
{
    const auto vars = artistSplitPromptVars(group);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/artist_split"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("artist_split"),
        .description = QStringLiteral("Artist split output"),
        .schema = artistSplitSchema(),
    };

    ai::ChatRequest req;
    req.messages = ai::toMessages(renderedRes.value());

    ai::LlmCall call {
        .purpose = ai::Purpose::Cleanup,
        .request = std::move(req),
        .structured = spec,
        .stream = false,
        .cachePolicy = ai::CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    auto task = m_llm.start(std::move(call));
    auto *taskPtr = task.get();

    QObject::connect(taskPtr, &ai::LlmTask::finished, taskPtr,
        [this, taskPtr, batchId, autoAcceptThreshold, group, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kArtistSplitInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedProposals = parseArtistSplitResult(*llmResult.structured, group);
            if (!parsedProposals.ok()) {
                done(parsedProposals.error());
                return;
            }

            if (!parsedProposals.value().isEmpty()) {
                library::CorrectionStore store(m_db, m_clock);
                auto addRes
                    = store.addProposals(batchId, parsedProposals.value(), autoAcceptThreshold);
                if (!addRes.ok()) {
                    done(addRes);
                    return;
                }
            }

            done({ });
        });

    return task;
}

} // namespace linernotes::butler
