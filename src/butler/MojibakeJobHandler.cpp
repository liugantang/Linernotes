// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MojibakeJobHandler.h"

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/Errors.h>
#include <butler/MojibakeAnalysis.h>
#include <butler/MojibakeLlm.h>
#include <butler/MojibakeSource.h>
#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/TrackIssueStore.h>

#include <utility>

namespace linernotes::butler {

namespace {

core::Result<void> recordIrreparableIssues(library::Database &db, const core::Clock &clock,
    const QList<std::pair<qint64, library::TagField>> &irreparable)
{
    if (irreparable.isEmpty()) {
        return { };
    }
    library::TrackIssueStore issueStore(db, clock);
    for (const auto &[trackId, field] : irreparable) {
        auto res = issueStore.add(trackId, library::TrackIssueKind::NeedsOnlineLookup, field,
            QStringLiteral("mojibake_irreparable"));
        if (!res.ok()) {
            return res;
        }
    }
    return { };
}

} // namespace

MojibakeJobHandler::MojibakeJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString MojibakeJobHandler::kind() const
{
    return QStringLiteral("butler.mojibake");
}

ai::TokenUsage MojibakeJobHandler::estimate(const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const MojibakeSource source(m_db);
    const auto groupRes = source.loadGroup(itemKey);
    if (!groupRes.ok()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto analysis = analyzeGroup(groupRes.value());
    if (analysis.ambiguous.isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto vars = mojibakePromptVars(groupRes.value(), analysis.ambiguous);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/mojibake"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    }
    const int completionTokens = static_cast<int>(analysis.ambiguous.size()) * 60;

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> MojibakeJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const MojibakeSource source(m_db);
    const auto groupRes = source.loadGroup(itemKey);
    if (!groupRes.ok()) {
        done(groupRes.error());
        return nullptr;
    }

    const auto analysis = analyzeGroup(groupRes.value());

    if (auto issueRes = recordIrreparableIssues(m_db, m_clock, analysis.irreparable);
        !issueRes.ok()) {
        done(issueRes);
        return nullptr;
    }

    const qint64 batchId = params.value(QStringLiteral("batchId")).toInteger(0);
    if (batchId <= 0) {
        done(core::Error {
            .code = QString(errc::kMojibakeInvalidKey),
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

    library::CorrectionStore correctionStore(m_db, m_clock);
    if (!analysis.proposals.isEmpty()) {
        auto addRes
            = correctionStore.addProposals(batchId, analysis.proposals, autoAcceptThreshold);
        if (!addRes.ok()) {
            done(addRes);
            return nullptr;
        }
    }

    if (analysis.ambiguous.isEmpty()) {
        done({ });
        return nullptr;
    }

    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        const auto fbProposals = fallbackProposals(analysis.ambiguous);
        if (!fbProposals.isEmpty()) {
            auto addRes = correctionStore.addProposals(batchId, fbProposals, autoAcceptThreshold);
            if (!addRes.ok()) {
                done(addRes);
                return nullptr;
            }
        }
        done({ });
        return nullptr;
    }

    return startLlm(
        groupRes.value(), analysis.ambiguous, batchId, autoAcceptThreshold, std::move(done));
}

std::unique_ptr<QObject> MojibakeJobHandler::startLlm(const MojibakeGroup &group,
    const QList<AmbiguousItem> &ambiguous, qint64 batchId,
    std::optional<double> autoAcceptThreshold, std::function<void(const core::Result<void> &)> done)
{
    const auto vars = mojibakePromptVars(group, ambiguous);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/mojibake"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("mojibake_cleanup"),
        .description = QStringLiteral("Mojibake cleanup output"),
        .schema = mojibakeSchema(),
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
        [this, taskPtr, batchId, autoAcceptThreshold, ambiguous, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kMojibakeInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedProposals = parseMojibakeResult(*llmResult.structured, ambiguous);
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
