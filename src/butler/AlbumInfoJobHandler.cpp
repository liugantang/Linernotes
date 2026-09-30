// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AlbumInfoJobHandler.h"

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/AlbumInfoLlm.h>
#include <butler/AlbumInfoSource.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

namespace {

struct ProcessParams {
    qint64 batchId = 0;
    std::optional<double> autoAcceptThreshold;
    bool useLlm = true;
};

core::Result<ProcessParams> parseProcessParams(const QJsonObject &params)
{
    const qint64 batchId = params.value(QStringLiteral("batchId")).toInteger(0);
    if (batchId <= 0) {
        return core::Error {
            .code = QString(errc::kAlbumInfoInvalidKey),
            .message = QStringLiteral("Missing or invalid batchId in params"),
            .detail = QString::number(batchId),
        };
    }

    std::optional<double> autoAcceptThreshold;
    if (params.contains(QStringLiteral("autoAccept"))
        && !params.value(QStringLiteral("autoAccept")).isNull()) {
        autoAcceptThreshold = params.value(QStringLiteral("autoAccept")).toDouble();
    }

    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);

    return ProcessParams {
        .batchId = batchId,
        .autoAcceptThreshold = autoAcceptThreshold,
        .useLlm = useLlm,
    };
}

core::Result<void> saveProposals(library::CorrectionStore &corrStore, qint64 batchId,
    const AlbumInfoProposals &proposals, const std::optional<double> &autoAcceptThreshold)
{
    if (!proposals.fromEvidence.isEmpty()) {
        const auto addRes
            = corrStore.addProposals(batchId, proposals.fromEvidence, autoAcceptThreshold);
        if (!addRes.ok()) {
            return addRes.error();
        }
    }

    if (!proposals.fromKnowledge.isEmpty()) {
        const auto addRes = corrStore.addProposals(batchId, proposals.fromKnowledge, std::nullopt);
        if (!addRes.ok()) {
            return addRes.error();
        }
    }

    return { };
}

core::Result<void> handleLlmResult(library::Database &db, const core::Clock &clock,
    const ai::LlmResult &llmResult, const QList<AlbumInfoInput> &albums,
    const QList<qint64> &albumIds, qint64 batchId, const std::optional<double> &autoAcceptThreshold,
    int promptVersion)
{
    if (!llmResult.structured.has_value()) {
        return core::Error {
            .code = QString(errc::kAlbumInfoInvalidResult),
            .message = QStringLiteral("Missing structured output from LLM"),
            .detail = { },
        };
    }

    const auto proposalsRes = buildAlbumInfoProposals(*llmResult.structured, albums);
    if (!proposalsRes.ok()) {
        return proposalsRes.error();
    }

    library::CorrectionStore corrStore(db, clock);
    const auto saveRes
        = saveProposals(corrStore, batchId, proposalsRes.value(), autoAcceptThreshold);
    if (!saveRes.ok()) {
        return saveRes.error();
    }

    const AlbumInfoSource checker(db);
    return checker.markChecked(albumIds, llmResult.model, promptVersion, clock.nowMs());
}

} // namespace

AlbumInfoJobHandler::AlbumInfoJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString AlbumInfoJobHandler::kind() const
{
    return QStringLiteral("butler.album_info");
}

int AlbumInfoJobHandler::maxInFlight() const
{
    return 2;
}

ai::TokenUsage AlbumInfoJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto albumIdsRes = parseAlbumInfoItemKey(itemKey);
    if (!albumIdsRes.ok() || albumIdsRes.value().isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const AlbumInfoSource source(m_db);
    const auto loadRes = source.load(albumIdsRes.value());
    if (!loadRes.ok() || loadRes.value().isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto &albums = loadRes.value();
    const auto vars = albumInfoPromptVars(albums);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/album_info"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        int totalTracks = 0;
        for (const auto &a : albums) {
            totalTracks += static_cast<int>(a.tracks.size());
        }
        promptTokens = (totalTracks * 50) + 300;
    }

    int totalTracks = 0;
    for (const auto &a : albums) {
        totalTracks += static_cast<int>(a.tracks.size());
    }

    constexpr int kFixedReasoningTokensPerBatch = 5000;
    constexpr int kCompletionTokensPerTrack = 100;
    const int completionTokens
        = kFixedReasoningTokensPerBatch + (totalTracks * kCompletionTokensPerTrack);

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> AlbumInfoJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const auto albumIdsRes = parseAlbumInfoItemKey(itemKey);
    if (!albumIdsRes.ok()) {
        done(albumIdsRes.error());
        return nullptr;
    }

    const auto &albumIds = albumIdsRes.value();
    if (albumIds.isEmpty()) {
        done({ });
        return nullptr;
    }

    const auto procParamsRes = parseProcessParams(params);
    if (!procParamsRes.ok()) {
        done(procParamsRes.error());
        return nullptr;
    }

    const auto &procParams = procParamsRes.value();
    if (!procParams.useLlm) {
        done({ });
        return nullptr;
    }

    const AlbumInfoSource source(m_db);
    const auto loadRes = source.load(albumIds);
    if (!loadRes.ok()) {
        done(loadRes.error());
        return nullptr;
    }

    const auto &albums = loadRes.value();
    if (albums.isEmpty()) {
        done({ });
        return nullptr;
    }

    const auto vars = albumInfoPromptVars(albums);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/album_info"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("album_info"),
        .description = QStringLiteral("Album info completion output"),
        .schema = albumInfoSchema(),
    };

    ai::ChatRequest req;
    req.messages = ai::toMessages(renderedRes.value());

    ai::LlmCall call {
        .purpose = ai::Purpose::Cleanup,
        .request = std::move(req),
        .structured = spec,
        .stream = true,
        .cachePolicy = ai::CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    const int promptVersion = renderedRes.value().version;
    auto task = m_llm.start(std::move(call));
    auto *taskPtr = task.get();

    QObject::connect(taskPtr, &ai::LlmTask::finished, taskPtr,
        [this, taskPtr, albums, albumIds, procParams, promptVersion, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto handleRes = handleLlmResult(m_db, m_clock, res.value(), albums, albumIds,
                procParams.batchId, procParams.autoAcceptThreshold, promptVersion);
            done(handleRes);
        });

    return task;
}

} // namespace linernotes::butler
