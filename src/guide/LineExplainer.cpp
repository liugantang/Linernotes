// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LineExplainer.h"

#include <ai/AiEnums.h>

#include <utility>

namespace linernotes::guide {

namespace {

QStringList contextAround(const ParsedLyrics &lyrics, int lineIndex)
{
    if (lineIndex < 0 || lineIndex >= lyrics.lines.size()) {
        return { };
    }
    QStringList before;
    for (int i = lineIndex - 1; i >= 0 && before.size() < 2; --i) {
        const auto &line = lyrics.lines.at(i);
        if (!line.text.trimmed().isEmpty()) {
            before.prepend(line.text);
        }
    }
    QStringList after;
    for (qsizetype i = lineIndex + 1; i < lyrics.lines.size() && after.size() < 2; ++i) {
        const auto &line = lyrics.lines.at(i);
        if (!line.text.trimmed().isEmpty()) {
            after.append(line.text);
        }
    }
    return before + after;
}

} // namespace

QHash<QString, QString> explainLinePromptVars(const LineExplainRequest &req)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("title"), req.title);
    vars.insert(QStringLiteral("artist"), req.artist);
    vars.insert(QStringLiteral("album"), req.album);

    // 1. Line: target line original text
    QString targetLine;
    if (req.lineIndex >= 0 && req.lineIndex < req.lyrics.lines.size()) {
        targetLine = req.lyrics.lines.at(req.lineIndex).text;
    }
    vars.insert(QStringLiteral("line"), targetLine);

    // 2. Context: lineIndex 前后各 2 个非空行（原样文本，在开头/结尾处截断）
    const QStringList contextLines = contextAround(req.lyrics, req.lineIndex);
    vars.insert(QStringLiteral("context"), contextLines.join(QLatin1Char('\n')));

    // 3. Lyrics: 整首歌词，每行一句，不带时间戳；有译文的行在原文后加 " / 译文"；跳过空行
    QStringList lyricsLines;
    for (qsizetype i = 0; i < req.lyrics.lines.size(); ++i) {
        const auto &line = req.lyrics.lines.at(i);
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        QString trans;
        if (i < req.translations.size() && !req.translations.at(i).trimmed().isEmpty()) {
            trans = req.translations.at(i).trimmed();
        } else if (!line.translation.trimmed().isEmpty()) {
            trans = line.translation.trimmed();
        }
        if (!trans.isEmpty()) {
            lyricsLines.append(line.text + QStringLiteral(" / ") + trans);
        } else {
            lyricsLines.append(line.text);
        }
    }
    vars.insert(QStringLiteral("lyrics"), lyricsLines.join(QLatin1Char('\n')));

    return vars;
}

LineExplainer::LineExplainer(ai::LlmService &llm, ai::PromptLibrary &prompts, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
    , m_prompts(prompts)
{
}

LineExplainer::~LineExplainer()
{
    cancel();
}

void LineExplainer::explain(const LineExplainRequest &req)
{
    cancel();

    if (req.trackId <= 0 || req.lineIndex < 0 || req.lineIndex >= req.lyrics.lines.size()) {
        return;
    }

    const auto vars = explainLinePromptVars(req);
    const auto renderedRes = m_prompts.render(QStringLiteral("guide/explain_line"), vars);
    if (!renderedRes.ok()) {
        emit finished(req.trackId, req.lineIndex, renderedRes.error());
        return;
    }

    ai::ChatRequest chatReq;
    chatReq.messages = ai::toMessages(renderedRes.value());

    ai::LlmCall call {
        .purpose = ai::Purpose::Guide,
        .request = std::move(chatReq),
        .structured = std::nullopt,
        .stream = true,
        .cachePolicy = ai::CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    m_activeTrackId = req.trackId;
    m_activeLineIndex = req.lineIndex;
    m_currentTask = m_llm.start(std::move(call));
    auto *taskPtr = m_currentTask.get();

    connect(taskPtr, &ai::LlmTask::delta, this,
        [this, taskPtr, trackId = req.trackId, lineIndex = req.lineIndex](const QString &text) {
            if (m_currentTask.get() != taskPtr) {
                return;
            }
            emit delta(trackId, lineIndex, text);
        });

    connect(taskPtr, &ai::LlmTask::finished, this,
        [this, taskPtr, trackId = req.trackId, lineIndex = req.lineIndex]() {
            if (m_currentTask.get() != taskPtr) {
                return;
            }
            // 移到局部变量：任务在本槽结束时才销毁（LlmTask 允许在 finished 槽里销毁）
            const auto task = std::move(m_currentTask);
            m_activeTrackId = -1;
            m_activeLineIndex = -1;

            const auto &res = task->result();
            if (!res.ok()) {
                emit finished(trackId, lineIndex, res.error());
                return;
            }

            const auto &llmResult = res.value();
            const QString fullText = llmResult.response.content.trimmed();
            emit finished(trackId, lineIndex, fullText);
        });
}

void LineExplainer::cancel()
{
    if (m_currentTask != nullptr) {
        auto task = std::move(m_currentTask);
        task->abort();
    }
    m_activeTrackId = -1;
    m_activeLineIndex = -1;
}

} // namespace linernotes::guide
