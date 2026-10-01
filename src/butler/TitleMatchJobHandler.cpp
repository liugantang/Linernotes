// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/Errors.h>
#include <butler/TitleMatchJobHandler.h>
#include <butler/TitleMatchLlm.h>
#include <butler/TitleMatchSource.h>
#include <butler/TitleMatchStore.h>
#include <core/Clock.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

TitleMatchJobHandler::TitleMatchJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString TitleMatchJobHandler::kind() const
{
    return QStringLiteral("butler.title_match");
}

ai::TokenUsage TitleMatchJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto pairsRes = parseTitlePairItemKey(itemKey);
    if (!pairsRes.ok() || pairsRes.value().isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto &pairs = pairsRes.value();
    const auto vars = titleMatchPromptVars(pairs);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/title_match"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        promptTokens = (static_cast<int>(pairs.size()) * 30) + 200;
    }

    constexpr int kFixedReasoningTokensPerBatch = 5000;
    constexpr int kCompletionTokensPerItem = 80;
    const int completionTokens = kFixedReasoningTokensPerBatch
        + (static_cast<int>(pairs.size()) * kCompletionTokensPerItem);

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> TitleMatchJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const auto pairsRes = parseTitlePairItemKey(itemKey);
    if (!pairsRes.ok()) {
        done(pairsRes.error());
        return nullptr;
    }

    const auto &pairs = pairsRes.value();
    if (pairs.isEmpty()) {
        done({ });
        return nullptr;
    }

    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        done({ });
        return nullptr;
    }

    const auto vars = titleMatchPromptVars(pairs);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/title_match"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("title_match"),
        .description = QStringLiteral("Title match output"),
        .schema = titleMatchSchema(),
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
        [this, taskPtr, pairs, promptVersion, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kTitleMatchInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedRes = parseTitleMatchResult(*llmResult.structured, pairs);
            if (!parsedRes.ok()) {
                done(parsedRes.error());
                return;
            }

            const TitleMatchStore store(m_db, m_clock);
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
