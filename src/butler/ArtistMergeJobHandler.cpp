// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistMergeJobHandler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/ArtistMerge.h>
#include <butler/ArtistMergeLlm.h>
#include <butler/ArtistMergeSource.h>
#include <butler/ArtistName.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

ArtistMergeJobHandler::ArtistMergeJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString ArtistMergeJobHandler::kind() const
{
    return QStringLiteral("butler.artist_merge");
}

ai::TokenUsage ArtistMergeJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const QJsonDocument doc = QJsonDocument::fromJson(itemKey.toUtf8());
    if (!doc.isObject()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const QJsonObject keyObj = doc.object();
    const QString type = keyObj.value(QStringLiteral("type")).toString();
    if (type != QLatin1StringView("confirm")) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const QJsonArray groupsArr = keyObj.value(QStringLiteral("groups")).toArray();
    if (groupsArr.isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    int totalMembers = 0;
    for (const auto &gElem : groupsArr) {
        if (gElem.isArray()) {
            totalMembers += static_cast<int>(gElem.toArray().size());
        }
    }

    constexpr int kPromptTokensPerMember = 60;
    constexpr int kPromptTokensBase = 300;
    // Reasoning overhead is per-request
    constexpr int kFixedReasoningTokensPerRequest = 8000;
    constexpr int kCompletionTokensPerGroup = 80;

    const int promptTokens = (totalMembers * kPromptTokensPerMember) + kPromptTokensBase;
    const int completionTokens = kFixedReasoningTokensPerRequest
        + (static_cast<int>(groupsArr.size()) * kCompletionTokensPerGroup);

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> ArtistMergeJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const QJsonDocument doc = QJsonDocument::fromJson(itemKey.toUtf8());
    if (!doc.isObject()) {
        done(core::Error {
            .code = QString(errc::kArtistMergeInvalidKey),
            .message = QStringLiteral("Invalid item key JSON"),
            .detail = itemKey,
        });
        return nullptr;
    }

    const qint64 batchId = params.value(QStringLiteral("batchId")).toInteger(0);
    if (batchId <= 0) {
        done(core::Error {
            .code = QString(errc::kArtistMergeInvalidKey),
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

    const QJsonObject keyObj = doc.object();
    const QString type = keyObj.value(QStringLiteral("type")).toString();

    if (type == QLatin1StringView("group")) {
        return processGroup(keyObj, batchId, autoAcceptThreshold, done);
    }
    if (type == QLatin1StringView("confirm")) {
        return processConfirm(keyObj, params, batchId, autoAcceptThreshold, std::move(done));
    }

    done(core::Error {
        .code = QString(errc::kArtistMergeInvalidKey),
        .message = QStringLiteral("Unknown item type in key"),
        .detail = type,
    });
    return nullptr;
}

std::unique_ptr<QObject> ArtistMergeJobHandler::processGroup(const QJsonObject &keyObj,
    qint64 batchId, std::optional<double> autoAcceptThreshold,
    const std::function<void(const core::Result<void> &)> &done)
{
    const QJsonArray idsArr = keyObj.value(QStringLiteral("ids")).toArray();
    if (idsArr.size() < 2) {
        done({ });
        return nullptr;
    }

    const ArtistMergeSource source(m_db);
    const auto artistsRes = source.loadArtists();
    if (!artistsRes.ok()) {
        done(artistsRes.error());
        return nullptr;
    }

    QHash<qint64, ArtistEntry> entriesById;
    for (const auto &entry : artistsRes.value()) {
        entriesById.insert(entry.artistId, entry);
    }

    QList<ArtistEntry> memberEntries;
    memberEntries.reserve(idsArr.size());
    for (const auto &elem : idsArr) {
        const qint64 id = elem.toInteger();
        if (entriesById.contains(id)) {
            memberEntries.append(entriesById.value(id));
        }
    }

    if (memberEntries.size() < 2) {
        done({ });
        return nullptr;
    }

    const ArtistGroup group {
        .members = memberEntries,
        .exactOnly = true,
    };

    const auto proposals = groupProposals(group);
    if (!proposals.isEmpty()) {
        library::CorrectionStore store(m_db, m_clock);
        auto addRes = store.addArtistAliasProposals(batchId, proposals, autoAcceptThreshold);
        if (!addRes.ok()) {
            done(addRes);
            return nullptr;
        }
    }

    done({ });
    return nullptr;
}

std::unique_ptr<QObject> ArtistMergeJobHandler::processConfirm(const QJsonObject &keyObj,
    const QJsonObject &params, qint64 batchId, std::optional<double> autoAcceptThreshold,
    std::function<void(const core::Result<void> &)> done)
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        done({ });
        return nullptr;
    }

    const QJsonArray groupsArr = keyObj.value(QStringLiteral("groups")).toArray();
    if (groupsArr.isEmpty()) {
        done({ });
        return nullptr;
    }

    const ArtistMergeSource source(m_db);
    const auto artistsRes = source.loadArtists();
    if (!artistsRes.ok()) {
        done(artistsRes.error());
        return nullptr;
    }

    QHash<qint64, ArtistEntry> entriesById;
    for (const auto &entry : artistsRes.value()) {
        entriesById.insert(entry.artistId, entry);
    }

    const auto altNamesRes = source.loadAltNames();
    const auto altNames = altNamesRes.ok() ? altNamesRes.value() : QHash<QString, QStringList> { };

    QList<ArtistMergeGroup> mergeGroups;
    int nextGroupId = 0;

    for (const auto &elem : groupsArr) {
        if (!elem.isArray()) {
            continue;
        }
        const auto idArr = elem.toArray();
        QList<ArtistMergeMember> members;
        for (const auto &idVal : idArr) {
            const qint64 id = idVal.toInteger();
            if (!entriesById.contains(id)) {
                continue;
            }
            const auto entry = entriesById.value(id);
            const auto albumsRes = source.sampleAlbums(id, 3);
            const QStringList albums = albumsRes.ok() ? albumsRes.value() : QStringList { };
            const QString key = exactKey(entry.name);
            const QStringList aka = altNames.value(key);

            members.append(ArtistMergeMember {
                .entry = entry,
                .albums = albums,
                .aka = aka,
            });
        }

        if (members.size() >= 2) {
            mergeGroups.append(ArtistMergeGroup {
                .id = nextGroupId++,
                .members = std::move(members),
            });
        }
    }

    if (mergeGroups.isEmpty()) {
        done({ });
        return nullptr;
    }

    return startLlm(mergeGroups, batchId, autoAcceptThreshold, std::move(done));
}

std::unique_ptr<QObject> ArtistMergeJobHandler::startLlm(const QList<ArtistMergeGroup> &groups,
    qint64 batchId, std::optional<double> autoAcceptThreshold,
    std::function<void(const core::Result<void> &)> done)
{
    const auto vars = artistMergePromptVars(groups);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/artist_merge"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("artist_merge"),
        .description = QStringLiteral("Artist merge output"),
        .schema = artistMergeSchema(),
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

    auto task = m_llm.start(std::move(call));
    auto *taskPtr = task.get();

    QObject::connect(taskPtr, &ai::LlmTask::finished, taskPtr,
        [this, taskPtr, batchId, autoAcceptThreshold, groups, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kArtistMergeInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedProposals = parseArtistMergeResult(*llmResult.structured, groups);
            if (!parsedProposals.ok()) {
                done(parsedProposals.error());
                return;
            }

            if (!parsedProposals.value().isEmpty()) {
                library::CorrectionStore store(m_db, m_clock);
                auto addRes = store.addArtistAliasProposals(
                    batchId, parsedProposals.value(), autoAcceptThreshold);
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
