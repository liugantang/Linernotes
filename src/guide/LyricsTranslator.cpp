// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsTranslator.h"

#include "Errors.h"
#include "LyricsTranslation.h"

#include <ai/AiEnums.h>
#include <ai/StructuredOutput.h>

namespace linernotes::guide {

LyricsTranslator::LyricsTranslator(ai::LlmService &llm, ai::PromptLibrary &prompts,
    library::Database &db, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_store(db, clock)
{
}

LyricsTranslator::~LyricsTranslator()
{
    cancel();
}

void LyricsTranslator::translate(qint64 trackId, const QString &rawText, const ParsedLyrics &lyrics,
    const QString &title, const QString &artist)
{
    cancel();

    if (trackId <= 0) {
        return;
    }

    const QString hash = lyricsHash(rawText);
    const auto cachedRes = m_store.get(trackId, hash);
    if (cachedRes.ok() && cachedRes.value().has_value()) {
        emit finished(trackId, *cachedRes.value());
        return;
    }

    const auto vars = lyricsTranslationPromptVars(lyrics, title, artist);
    const auto renderedRes = m_prompts.render(QStringLiteral("guide/translate_lyrics"), vars);
    if (!renderedRes.ok()) {
        emit finished(trackId, renderedRes.error());
        return;
    }

    const QJsonObject schema = lyricsTranslationSchema();
    if (schema.isEmpty()) {
        emit finished(trackId,
            core::Error {
                .code = QString(errc::kParseFailed),
                .message = QStringLiteral("Lyrics translation schema not found"),
                .detail = QStringLiteral(":/schemas/guide/translate_lyrics.json"),
            });
        return;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("translate_lyrics"),
        .description = QStringLiteral("Translate song lyrics to Simplified Chinese"),
        .schema = schema,
    };

    ai::ChatRequest req;
    req.messages = ai::toMessages(renderedRes.value());

    ai::LlmCall call {
        .purpose = ai::Purpose::Guide,
        .request = std::move(req),
        .structured = spec,
        .stream = true,
        .cachePolicy = ai::CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    m_activeTrackId = trackId;
    m_currentTask = m_llm.start(std::move(call));
    auto *taskPtr = m_currentTask.get();

    const int promptVersion = renderedRes.value().version;

    connect(taskPtr, &ai::LlmTask::finished, this,
        [this, taskPtr, trackId, lyrics, hash, promptVersion]() {
            if (m_currentTask.get() != taskPtr) {
                return;
            }
            m_currentTask.reset();
            m_activeTrackId = -1;

            const auto &res = taskPtr->result();
            if (!res.ok()) {
                emit finished(trackId, res.error());
                return;
            }

            const auto &llmResult = res.value();
            if (!llmResult.structured.has_value()) {
                emit finished(trackId,
                    core::Error {
                        .code = QString(errc::kParseFailed),
                        .message = QStringLiteral("Missing structured output from LLM"),
                        .detail = QString(),
                    });
                return;
            }

            const auto parsedRes = parseLyricsTranslation(*llmResult.structured, lyrics);
            if (!parsedRes.ok()) {
                emit finished(trackId, parsedRes.error());
                return;
            }

            const auto saveRes
                = m_store.save(trackId, hash, parsedRes.value(), llmResult.model, promptVersion);
            if (!saveRes.ok()) {
                // Non-fatal, return the parsed translation anyway
            }

            emit finished(trackId, parsedRes.value());
        });
}

void LyricsTranslator::cancel()
{
    if (m_currentTask != nullptr) {
        auto task = std::move(m_currentTask);
        task->abort();
    }
    m_activeTrackId = -1;
}

} // namespace linernotes::guide
