// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <guide/LrcParser.h>
#include <guide/LyricsTranslationStore.h>
#include <library/Database.h>

#include <memory>

namespace linernotes::guide {

class LyricsTranslator : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LyricsTranslator)

public:
    explicit LyricsTranslator(ai::LlmService &llm, ai::PromptLibrary &prompts,
        library::Database &db, const core::Clock &clock, QObject *parent = nullptr);
    ~LyricsTranslator() override;

    // 先查 Store，命中则直接（可同步）emit；否则调 LLM：Purpose::Guide、structured、stream = true、
    // cachePolicy Use、dataCategories 为空。成功后 save（model 用 LlmResult::model，promptVersion
    // 用渲染结果里的版本）。
    void translate(qint64 trackId, const QString &rawText, const ParsedLyrics &lyrics,
        const QString &title, const QString &artist);
    void cancel(); // 中止进行中的任务（切歌时调用）

signals:
    void finished(qint64 trackId, const linernotes::core::Result<QStringList> &result);

private:
    ai::LlmService &m_llm;
    ai::PromptLibrary &m_prompts;
    LyricsTranslationStore m_store;

    std::unique_ptr<ai::LlmTask> m_currentTask;
    qint64 m_activeTrackId = -1;
};

} // namespace linernotes::guide
