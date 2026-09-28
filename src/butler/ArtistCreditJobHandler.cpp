// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ArtistCreditJobHandler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/ArtistCredit.h>
#include <butler/ArtistCreditLlm.h>
#include <butler/ArtistCreditSource.h>
#include <butler/ArtistCreditStore.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

namespace {

struct ItemKeyData {
    QString type { };
    QStringList values;
};

struct JobParamsData {
    qint64 batchId = 0;
    std::optional<double> autoAcceptThreshold;
};

core::Result<ItemKeyData> parseItemKey(const QString &itemKey)
{
    const auto doc = QJsonDocument::fromJson(itemKey.toUtf8());
    if (!doc.isObject()) {
        return core::Error {
            .code = QString(errc::kArtistCreditInvalidKey),
            .message = QStringLiteral("Failed to parse item key JSON object"),
            .detail = itemKey,
        };
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    const QJsonArray valArr = obj.value(QStringLiteral("values")).toArray();

    QStringList values;
    values.reserve(valArr.size());
    for (const auto &v : valArr) {
        values.append(v.toString());
    }

    return ItemKeyData {
        .type = type,
        .values = std::move(values),
    };
}

core::Result<JobParamsData> parseJobParams(const QJsonObject &params)
{
    const qint64 batchId = params.value(QStringLiteral("batchId")).toInteger(0);
    if (batchId <= 0) {
        return core::Error {
            .code = QString(errc::kArtistCreditInvalidKey),
            .message = QStringLiteral("Missing or invalid batchId in params"),
            .detail = QString::number(batchId),
        };
    }

    std::optional<double> autoAcceptThreshold;
    if (params.contains(QStringLiteral("autoAccept"))
        && !params.value(QStringLiteral("autoAccept")).isNull()) {
        autoAcceptThreshold = params.value(QStringLiteral("autoAccept")).toDouble();
    }

    return JobParamsData {
        .batchId = batchId,
        .autoAcceptThreshold = autoAcceptThreshold,
    };
}

} // namespace

ArtistCreditJobHandler::ArtistCreditJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString ArtistCreditJobHandler::kind() const
{
    return QStringLiteral("butler.artist_credit");
}

ai::TokenUsage ArtistCreditJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto keyRes = parseItemKey(itemKey);
    if (!keyRes.ok()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto &keyData = keyRes.value();
    if (keyData.type != QLatin1StringView("parse") || keyData.values.isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto vars = artistCreditPromptVars(keyData.values);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/artist_credit"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        promptTokens = (static_cast<int>(keyData.values.size()) * 30) + 200;
    }
    const int completionTokens = static_cast<int>(keyData.values.size()) * 40;

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

core::Result<void> ArtistCreditJobHandler::addCreditProposals(
    const QHash<QString, ArtistCredit> &credits, qint64 batchId,
    std::optional<double> autoAcceptThreshold) const
{
    const ArtistCreditSource source(m_db);
    QList<library::CorrectionProposal> proposals;

    for (auto it = credits.cbegin(); it != credits.cend(); ++it) {
        const QString &val = it.key();
        const ArtistCredit &credit = it.value();
        const QString norm = normalizedValue(credit);
        if (norm == val) {
            continue;
        }

        auto targetsRes = source.targetsFor(val);
        if (!targetsRes.ok()) {
            return targetsRes.error();
        }

        for (const auto &target : targetsRes.value()) {
            proposals.append(library::CorrectionProposal {
                .trackId = target.trackId,
                .field = target.field,
                .oldValue = val,
                .newValue = norm,
                .source = library::CorrectionSource::Llm,
                .confidence = credit.confidence,
                .reason = credit.reason,
            });
        }
    }

    if (!proposals.isEmpty()) {
        library::CorrectionStore corrStore(m_db, m_clock);
        auto addRes = corrStore.addProposals(batchId, proposals, autoAcceptThreshold);
        if (!addRes.ok()) {
            return addRes;
        }
    }

    return { };
}

std::unique_ptr<QObject> ArtistCreditJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    auto keyRes = parseItemKey(itemKey);
    if (!keyRes.ok()) {
        done(keyRes.error());
        return nullptr;
    }

    auto paramsRes = parseJobParams(params);
    if (!paramsRes.ok()) {
        done(paramsRes.error());
        return nullptr;
    }

    const auto &keyData = keyRes.value();
    const auto &paramsData = paramsRes.value();

    if (keyData.type == QLatin1StringView("cached")) {
        const ArtistCreditStore store(m_db, m_clock);
        QHash<QString, ArtistCredit> credits;

        for (const auto &val : keyData.values) {
            auto loadRes = store.load(val);
            if (!loadRes.ok()) {
                continue;
            }
            const auto &storedOpt = loadRes.value();
            if (storedOpt.has_value()) {
                credits.insert(val, storedOpt->credit);
            }
        }

        auto addRes
            = addCreditProposals(credits, paramsData.batchId, paramsData.autoAcceptThreshold);
        if (!addRes.ok()) {
            done(addRes);
            return nullptr;
        }

        done({ });
        return nullptr;
    }

    if (keyData.type == QLatin1StringView("parse")) {
        const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
        if (!useLlm) {
            done({ });
            return nullptr;
        }
        return startLlm(
            keyData.values, paramsData.batchId, paramsData.autoAcceptThreshold, std::move(done));
    }

    done(core::Error {
        .code = QString(errc::kArtistCreditInvalidKey),
        .message = QStringLiteral("Unknown item type in key"),
        .detail = keyData.type,
    });
    return nullptr;
}

std::unique_ptr<QObject> ArtistCreditJobHandler::startLlm(const QStringList &values, qint64 batchId,
    std::optional<double> autoAcceptThreshold, std::function<void(const core::Result<void> &)> done)
{
    const auto vars = artistCreditPromptVars(values);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/artist_credit"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("artist_credit"),
        .description = QStringLiteral("Artist credit output"),
        .schema = artistCreditSchema(),
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

    const int promptVersion = renderedRes.value().version;
    auto task = m_llm.start(std::move(call));
    auto *taskPtr = task.get();

    QObject::connect(taskPtr, &ai::LlmTask::finished, taskPtr,
        [this, taskPtr, batchId, autoAcceptThreshold, values, promptVersion,
            done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kArtistCreditInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedCredits = parseArtistCreditResult(*llmResult.structured, values);
            if (!parsedCredits.ok()) {
                done(parsedCredits.error());
                return;
            }

            const auto &creditsMap = parsedCredits.value();
            ArtistCreditStore creditStore(m_db, m_clock);
            auto saveRes = creditStore.save(creditsMap, llmResult.model, promptVersion);
            if (!saveRes.ok()) {
                done(saveRes.error());
                return;
            }

            auto addRes = addCreditProposals(creditsMap, batchId, autoAcceptThreshold);
            if (!addRes.ok()) {
                done(addRes);
                return;
            }

            done({ });
        });

    return task;
}

} // namespace linernotes::butler
