// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <butler/Errors.h>
#include <butler/TranslationJobHandler.h>
#include <butler/TranslationLlm.h>
#include <butler/TranslationSource.h>
#include <butler/TranslationStore.h>
#include <core/Clock.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::butler {

TranslationJobHandler::TranslationJobHandler(library::Database &db, ai::LlmService &llm,
    const ai::PromptLibrary &prompts, const core::Clock &clock)
    : m_db(db)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_clock(clock)
{
}

QString TranslationJobHandler::kind() const
{
    return QStringLiteral("butler.translate");
}

ai::TokenUsage TranslationJobHandler::estimate(
    const QString &itemKey, const QJsonObject &params) const
{
    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto textsRes = parseTranslationItemKey(itemKey);
    if (!textsRes.ok() || textsRes.value().isEmpty()) {
        return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
    }

    const auto &texts = textsRes.value();
    const auto vars = translationPromptVars(texts);
    const auto rendered = m_prompts.render(QStringLiteral("cleanup/translate_titles"), vars);

    int promptTokens = 0;
    if (rendered.ok()) {
        promptTokens = ai::roughTokenCount(rendered.value().system)
            + ai::roughTokenCount(rendered.value().user);
    } else {
        promptTokens = (static_cast<int>(texts.size()) * 20) + 150;
    }

    constexpr int kFixedReasoningTokensPerBatch = 5000;
    constexpr int kCompletionTokensPerItem = 80;
    const int completionTokens = kFixedReasoningTokensPerBatch
        + (static_cast<int>(texts.size()) * kCompletionTokensPerItem);

    return ai::TokenUsage {
        .promptTokens = promptTokens,
        .completionTokens = completionTokens,
    };
}

std::unique_ptr<QObject> TranslationJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    const auto textsRes = parseTranslationItemKey(itemKey);
    if (!textsRes.ok()) {
        done(textsRes.error());
        return nullptr;
    }

    const auto &texts = textsRes.value();
    if (texts.isEmpty()) {
        done({ });
        return nullptr;
    }

    const bool useLlm = params.value(QStringLiteral("useLlm")).toBool(true);
    if (!useLlm) {
        done({ });
        return nullptr;
    }

    const auto vars = translationPromptVars(texts);
    const auto renderedRes = m_prompts.render(QStringLiteral("cleanup/translate_titles"), vars);
    if (!renderedRes.ok()) {
        done(renderedRes.error());
        return nullptr;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("translate_titles"),
        .description = QStringLiteral("Title and album translation output"),
        .schema = translationSchema(),
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
        [this, taskPtr, texts, promptVersion, done = std::move(done)]() {
            const auto &res = taskPtr->result();
            if (!res.ok()) {
                done(res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                done(core::Error {
                    .code = QString(errc::kTranslationInvalidResult),
                    .message = QStringLiteral("Missing structured output from LLM"),
                    .detail = QString(),
                });
                return;
            }

            const auto parsedRes = parseTranslationResult(*llmResult.structured, texts);
            if (!parsedRes.ok()) {
                done(parsedRes.error());
                return;
            }

            const TranslationStore store(m_db, m_clock);
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
