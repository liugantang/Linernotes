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
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

ArtistMergeJobHandler::ArtistMergeJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, MusicBrainzClient &mbClient, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_mbClient(mbClient)
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
    if (type != QLatin1StringView("fuzzy")) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const QJsonArray pairsArr = keyObj.value(QStringLiteral("pairs")).toArray();
    if (pairsArr.isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const ArtistMergeSource source(m_db);
    const auto artistsRes = source.loadArtists();
    if (!artistsRes.ok()) {
        const int count = static_cast<int>(pairsArr.size());
        return ai::TokenUsage {
            .promptTokens = (count * 80) + 200,
            .completionTokens = count * 40,
        };
    }

    QHash<qint64, ArtistEntry> entriesById;
    for (const auto &entry : artistsRes.value()) {
        entriesById.insert(entry.artistId, entry);
    }

    QList<ArtistMergeCandidatePair> candidatePairs;
    int candId = 0;
    for (const auto &elem : pairsArr) {
        if (!elem.isArray()) {
            continue;
        }
        const auto p = elem.toArray();
        if (p.size() < 2) {
            continue;
        }
        const qint64 idA = p.at(0).toInteger();
        const qint64 idB = p.at(1).toInteger();
        if (!entriesById.contains(idA) || !entriesById.contains(idB)) {
            continue;
        }
        const auto albumsA = source.sampleAlbums(idA);
        const auto albumsB = source.sampleAlbums(idB);
        candidatePairs.append(ArtistMergeCandidatePair {
            .id = candId++,
            .artistA = entriesById.value(idA),
            .albumsA = albumsA.ok() ? albumsA.value() : QStringList { },
            .artistB = entriesById.value(idB),
            .albumsB = albumsB.ok() ? albumsB.value() : QStringList { },
        });
    }

    if (candidatePairs.isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto vars = artistMergePromptVars(candidatePairs);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/artist_merge"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        promptTokens = (static_cast<int>(candidatePairs.size()) * 80) + 200;
    }
    const int completionTokens = static_cast<int>(candidatePairs.size()) * 40;

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

    if (type == QLatin1StringView("cluster")) {
        return processCluster(keyObj, batchId, autoAcceptThreshold, done);
    }
    if (type == QLatin1StringView("fuzzy")) {
        return processFuzzy(keyObj, params, batchId, autoAcceptThreshold, std::move(done));
    }
    if (type == QLatin1StringView("mb")) {
        return processMb(keyObj, batchId, autoAcceptThreshold, std::move(done));
    }

    done(core::Error {
        .code = QString(errc::kArtistMergeInvalidKey),
        .message = QStringLiteral("Unknown item type in key"),
        .detail = type,
    });
    return nullptr;
}

std::unique_ptr<QObject> ArtistMergeJobHandler::processCluster(const QJsonObject &keyObj,
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
        if (!entriesById.contains(id)) {
            done({ });
            return nullptr;
        }
        memberEntries.append(entriesById.value(id));
    }

    const auto clustering = clusterArtists(memberEntries);
    if (clustering.clusters.isEmpty()) {
        done({ });
        return nullptr;
    }

    library::CorrectionStore store(m_db, m_clock);
    for (const auto &cluster : clustering.clusters) {
        const auto proposals = clusterProposals(cluster);
        if (!proposals.isEmpty()) {
            auto addRes = store.addArtistAliasProposals(batchId, proposals, autoAcceptThreshold);
            if (!addRes.ok()) {
                done(addRes);
                return nullptr;
            }
        }
    }

    done({ });
    return nullptr;
}

std::unique_ptr<QObject> ArtistMergeJobHandler::processFuzzy(const QJsonObject &keyObj,
    const QJsonObject &params, qint64 batchId, std::optional<double> autoAcceptThreshold,
    std::function<void(const core::Result<void> &)> done)
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        done({ });
        return nullptr;
    }

    const QJsonArray pairsArr = keyObj.value(QStringLiteral("pairs")).toArray();
    if (pairsArr.isEmpty()) {
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

    QList<ArtistMergeCandidatePair> candidatePairs;
    int candId = 0;
    for (const auto &elem : pairsArr) {
        if (!elem.isArray()) {
            continue;
        }
        const auto p = elem.toArray();
        if (p.size() < 2) {
            continue;
        }
        const qint64 idA = p.at(0).toInteger();
        const qint64 idB = p.at(1).toInteger();
        if (!entriesById.contains(idA) || !entriesById.contains(idB)) {
            continue;
        }
        const auto albumsA = source.sampleAlbums(idA);
        const auto albumsB = source.sampleAlbums(idB);
        candidatePairs.append(ArtistMergeCandidatePair {
            .id = candId++,
            .artistA = entriesById.value(idA),
            .albumsA = albumsA.ok() ? albumsA.value() : QStringList { },
            .artistB = entriesById.value(idB),
            .albumsB = albumsB.ok() ? albumsB.value() : QStringList { },
        });
    }

    if (candidatePairs.isEmpty()) {
        done({ });
        return nullptr;
    }

    return startLlm(candidatePairs, entriesById, batchId, autoAcceptThreshold, std::move(done));
}

std::unique_ptr<QObject> ArtistMergeJobHandler::startLlm(
    const QList<ArtistMergeCandidatePair> &pairs, const QHash<qint64, ArtistEntry> &entriesById,
    qint64 batchId, std::optional<double> autoAcceptThreshold,
    std::function<void(const core::Result<void> &)> done)
{
    const auto vars = artistMergePromptVars(pairs);
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
        [this, taskPtr, batchId, autoAcceptThreshold, pairs, entriesById,
            done = std::move(done)]() {
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

            const auto parsedProposals
                = parseArtistMergeResult(*llmResult.structured, pairs, entriesById);
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

std::unique_ptr<QObject> ArtistMergeJobHandler::processMb(const QJsonObject &keyObj, qint64 batchId,
    std::optional<double> autoAcceptThreshold, std::function<void(const core::Result<void> &)> done)
{
    const qint64 id = keyObj.value(QStringLiteral("id")).toInteger(0);
    if (id <= 0) {
        done(core::Error {
            .code = QString(errc::kArtistMergeInvalidKey),
            .message = QStringLiteral("Invalid artist id in mb key"),
            .detail = QString::number(id),
        });
        return nullptr;
    }

    const ArtistMergeSource source(m_db);
    const auto artistsRes = source.loadArtists();
    if (!artistsRes.ok()) {
        done(artistsRes.error());
        return nullptr;
    }

    ArtistEntry ours;
    bool found = false;
    for (const auto &entry : artistsRes.value()) {
        if (entry.artistId == id) {
            ours = entry;
            found = true;
            break;
        }
    }

    if (!found) {
        done({ });
        return nullptr;
    }

    auto searchTask = m_mbClient.searchArtist(ours.name);
    auto *taskPtr = searchTask.get();

    QObject::connect(taskPtr, &MbSearchTask::finished, taskPtr,
        [this, taskPtr, ours, batchId, autoAcceptThreshold, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const ArtistMergeSource reloadSource(m_db);
            const auto reloadRes = reloadSource.loadArtists();
            if (!reloadRes.ok()) {
                done(reloadRes.error());
                return;
            }

            QHash<QString, ArtistEntry> libraryByName;
            for (const auto &entry : reloadRes.value()) {
                libraryByName.insert(entry.name, entry);
            }

            if (!libraryByName.contains(ours.name)) {
                done({ });
                return;
            }

            const auto currentOurs = libraryByName.value(ours.name);
            const auto proposals = musicBrainzProposals(currentOurs, res.value(), libraryByName);
            if (!proposals.isEmpty()) {
                library::CorrectionStore store(m_db, m_clock);
                auto addRes
                    = store.addArtistAliasProposals(batchId, proposals, autoAcceptThreshold);
                if (!addRes.ok()) {
                    done(addRes);
                    return;
                }
            }

            done({ });
        });

    return searchTask;
}

} // namespace linernotes::butler
