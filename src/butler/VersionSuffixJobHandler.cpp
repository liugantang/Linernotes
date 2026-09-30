// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/Errors.h>
#include <butler/VersionSuffixJobHandler.h>
#include <butler/VersionSuffixLlm.h>
#include <butler/VersionSuffixSource.h>
#include <butler/VersionSuffixStore.h>
#include <core/Clock.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

VersionSuffixJobHandler::VersionSuffixJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString VersionSuffixJobHandler::kind() const
{
    return QStringLiteral("butler.version_suffix");
}

ai::TokenUsage VersionSuffixJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto samplesRes = parseSuffixItemKey(itemKey);
    if (!samplesRes.ok() || samplesRes.value().isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto &samples = samplesRes.value();
    const auto vars = versionSuffixPromptVars(samples);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/version_suffix"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        promptTokens = (static_cast<int>(samples.size()) * 30) + 200;
    }

    constexpr int kFixedReasoningTokensPerBatch = 5000;
    constexpr int kCompletionTokensPerItem = 80;
    const int completionTokens = kFixedReasoningTokensPerBatch
        + (static_cast<int>(samples.size()) * kCompletionTokensPerItem);

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> VersionSuffixJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const auto samplesRes = parseSuffixItemKey(itemKey);
    if (!samplesRes.ok()) {
        done(samplesRes.error());
        return nullptr;
    }

    const auto &samples = samplesRes.value();
    if (samples.isEmpty()) {
        done({ });
        return nullptr;
    }

    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        done({ });
        return nullptr;
    }

    const auto vars = versionSuffixPromptVars(samples);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/version_suffix"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("version_suffix"),
        .description = QStringLiteral("Version suffix output"),
        .schema = versionSuffixSchema(),
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
        [this, taskPtr, samples, promptVersion, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kVersionSuffixInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedRes = parseVersionSuffixResult(*llmResult.structured, samples);
            if (!parsedRes.ok()) {
                done(parsedRes.error());
                return;
            }

            const VersionSuffixStore store(m_db, m_clock);
            auto saveRes = store.save(parsedRes.value(), llmResult.model, promptVersion);
            if (!saveRes.ok()) {
                done(saveRes.error());
                return;
            }

            done({ });
        });

    return task;
}

} // namespace linernotes::butler
