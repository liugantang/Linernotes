// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <core/Result.h>
#include <nlq/NlqQuery.h>

#include <memory>
#include <optional>

namespace linernotes::nlq {

struct Interpretation {
    Query query;
    QString explanation;
    bool operator==(const Interpretation &) const = default;
};

QHash<QString, QString> nlqPromptVars(
    const QString &question, const QString &librarySummary, const std::optional<Query> &previous);
QJsonObject nlqQuerySchema();
core::Result<Interpretation> parseInterpretation(const QJsonValue &value);

/// 一次问句解释。参照 ai::LlmTask 的用法：调用方持有，finished 恰好一次，销毁即中止。
class Interpreter : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Interpreter)

public:
    Interpreter(ai::LlmService &llm, ai::PromptLibrary &prompts, QObject *parent = nullptr);
    ~Interpreter() override;

    /// 开始解释；已有进行中的请求则先中止它。
    void interpret(const QString &question, const QString &librarySummary,
        const std::optional<Query> &previous);
    void cancel();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] const core::Result<Interpretation> &result() const; // finished 之后
    [[nodiscard]] ai::TokenUsage usage() const; // 最近一次的用量

signals:
    void finished();

private:
    ai::LlmService &m_llm;
    ai::PromptLibrary &m_prompts;
    std::unique_ptr<ai::LlmTask> m_currentTask;
    core::Result<Interpretation> m_result { core::Error { } };
    ai::TokenUsage m_usage;
    bool m_running = false;
};

} // namespace linernotes::nlq
