// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <ai/AiConfig.h>
#include <ai/ChatTypes.h>
#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <core/Result.h>
#include <guide/DeepDiveTools.h>

#include <memory>
#include <optional>

namespace linernotes::guide {

struct DeepDiveContext {
    qint64 trackId = 0;
    QString title { };
    QString artist { };
    QString album { };
    std::optional<int> year = std::nullopt;
    bool operator==(const DeepDiveContext &) const = default;
};

class DeepDiveSession : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DeepDiveSession)

public:
    DeepDiveSession(ai::LlmService &llm, ai::PromptLibrary &prompts, const ai::AiConfig &config,
        DeepDiveTools &tools, QObject *parent = nullptr);
    ~DeepDiveSession() override;

    void reset(const DeepDiveContext &context);
    void ask(const QString &question);
    void cancel();
    [[nodiscard]] bool busy() const;
    [[nodiscard]] const DeepDiveContext &context() const;
    [[nodiscard]] const QList<ai::ChatMessage> &history() const;

signals:
    void turnStarted();
    void toolUsed(const QString &name, const QString &summary);
    void delta(const QString &text);
    void turnFinished(const linernotes::core::Result<QString> &answer,
        const linernotes::guide::DeepDiveSourceList &sources);

private:
    void sendLlmRequest();
    void onLlmFinished(const core::Result<ai::LlmResult> &res);
    void executeNextToolCall();
    void onToolExecuted(
        const QString &callId, const QString &resultJson, const DeepDiveSourceList &sources);
    [[nodiscard]] DeepDiveSourceList dedupedSources() const;

    ai::LlmService &m_llm;
    ai::PromptLibrary &m_prompts;
    const ai::AiConfig &m_config;
    DeepDiveTools &m_tools;

    DeepDiveContext m_context;
    QList<ai::ChatMessage> m_history;
    bool m_busy = false;

    int m_toolStepsCount = 0;
    DeepDiveSourceList m_turnSources;
    QString m_currentUserQuestion;
    QString m_currentDeltaBuffer;
    QList<ai::ChatMessage> m_turnMessages;
    QList<ai::ToolCall> m_pendingToolCalls;
    int m_currentToolCallIndex = 0;

    std::unique_ptr<ai::LlmTask> m_currentTask;
};

} // namespace linernotes::guide
