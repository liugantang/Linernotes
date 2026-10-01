// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Interpreter.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <ai/AiEnums.h>
#include <ai/Errors.h>
#include <ai/LlmService.h>
#include <ai/StructuredOutput.h>
#include <nlq/Errors.h>

namespace linernotes::nlq {

QHash<QString, QString> nlqPromptVars(
    const QString &question, const QString &librarySummary, const std::optional<Query> &previous)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("library_summary"), librarySummary);

    QString prevStr = QStringLiteral("(none)");
    if (previous.has_value()) {
        const QJsonDocument doc(previous->toJson());
        prevStr = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    vars.insert(QStringLiteral("previous_query"), prevStr);
    vars.insert(QStringLiteral("question"), question);

    return vars;
}

QJsonObject nlqQuerySchema()
{
    QFile file(QStringLiteral(":/schemas/nlq/query.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return { };
    }
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.object();
}

core::Result<Interpretation> parseInterpretation(const QJsonValue &value)
{
    if (!value.isObject()) {
        return core::Error {
            .code = QString(errc::kInvalidResult),
            .message = QStringLiteral("Expected JSON object for interpretation"),
            .detail = QString(),
        };
    }

    const QJsonObject obj = value.toObject();
    if (!obj.contains(QStringLiteral("query")) || !obj.value(QStringLiteral("query")).isObject()) {
        return core::Error {
            .code = QString(errc::kInvalidResult),
            .message = QStringLiteral("Missing or invalid 'query' object in interpretation"),
            .detail = QString(),
        };
    }

    if (!obj.contains(QStringLiteral("explanation"))
        || !obj.value(QStringLiteral("explanation")).isString()) {
        return core::Error {
            .code = QString(errc::kInvalidResult),
            .message = QStringLiteral("Missing or invalid 'explanation' string in interpretation"),
            .detail = QString(),
        };
    }

    const auto queryRes = Query::fromJson(obj.value(QStringLiteral("query")).toObject());
    if (!queryRes.ok()) {
        return queryRes.error();
    }

    return Interpretation {
        .query = queryRes.value(),
        .explanation = obj.value(QStringLiteral("explanation")).toString(),
    };
}

Interpreter::Interpreter(ai::LlmService &llm, ai::PromptLibrary &prompts, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
    , m_prompts(prompts)
{
}

Interpreter::~Interpreter() = default;

void Interpreter::interpret(
    const QString &question, const QString &librarySummary, const std::optional<Query> &previous)
{
    cancel();

    const auto vars = nlqPromptVars(question, librarySummary, previous);
    const auto renderedRes = m_prompts.render(QStringLiteral("nlq/query"), vars);
    if (!renderedRes.ok()) {
        m_result = renderedRes.error();
        m_usage = { };
        m_running = false;
        QTimer::singleShot(0, this, &Interpreter::finished);
        return;
    }

    const QJsonObject schema = nlqQuerySchema();
    if (schema.isEmpty()) {
        m_result = core::Error {
            .code = QString(errc::kSchemaNotFound),
            .message = QStringLiteral("NLQ query schema not found"),
            .detail = QStringLiteral(":/schemas/nlq/query.json"),
        };
        m_usage = { };
        m_running = false;
        QTimer::singleShot(0, this, &Interpreter::finished);
        return;
    }

    const ai::StructuredSpec spec {
        .name = QStringLiteral("query"),
        .description = QStringLiteral("Library query translation"),
        .schema = schema,
    };

    ai::ChatRequest req;
    req.messages = ai::toMessages(renderedRes.value());

    ai::LlmCall call {
        .purpose = ai::Purpose::Query,
        .request = std::move(req),
        .structured = spec,
        .stream = true,
        .cachePolicy = ai::CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    m_currentTask = m_llm.start(std::move(call));
    m_running = true;
    auto *taskPtr = m_currentTask.get();

    connect(taskPtr, &ai::LlmTask::finished, this, [this, taskPtr]() {
        if (m_currentTask.get() != taskPtr) {
            return;
        }
        m_running = false;

        const auto &res = taskPtr->result();
        if (!res.ok()) {
            m_result = res.error();
            m_usage = { };
            emit finished();
            return;
        }

        const auto &llmResult = res.value();
        m_usage = llmResult.totalUsage;
        if (!llmResult.structured.has_value()) {
            m_result = core::Error {
                .code = QString(errc::kInvalidResult),
                .message = QStringLiteral("Missing structured output from LLM"),
                .detail = QString(),
            };
            emit finished();
            return;
        }

        m_result = parseInterpretation(*llmResult.structured);
        emit finished();
    });
}

void Interpreter::cancel()
{
    if (m_currentTask != nullptr) {
        auto task = std::move(m_currentTask);
        task->abort();
    }
    m_running = false;
}

bool Interpreter::isRunning() const
{
    return m_running;
}

const core::Result<Interpretation> &Interpreter::result() const
{
    return m_result;
}

ai::TokenUsage Interpreter::usage() const
{
    return m_usage;
}

} // namespace linernotes::nlq
