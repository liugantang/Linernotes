// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DeepDiveSession.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <ai/AiEnums.h>
#include <ai/Errors.h>

#include <utility>

namespace linernotes::guide {

namespace {

QString toolSummary(const QString &name, const QString &argumentsJson)
{
    if (name == QStringLiteral("lookup_knowledge")) {
        const auto doc = QJsonDocument::fromJson(argumentsJson.toUtf8());
        if (doc.isObject()) {
            const auto obj = doc.object();
            const QString artist = obj.value(QStringLiteral("artist")).toString().trimmed();
            const QString title = obj.value(QStringLiteral("title")).toString().trimmed();
            if (!title.isEmpty() && !artist.isEmpty()) {
                return QStringLiteral("查询 Wikipedia：%1 - %2").arg(artist, title);
            }
            if (!artist.isEmpty()) {
                return QStringLiteral("查询 Wikipedia：%1").arg(artist);
            }
            if (!title.isEmpty()) {
                return QStringLiteral("查询 Wikipedia：%1").arg(title);
            }
        }
        return QStringLiteral("查询 Wikipedia");
    }
    if (name == QStringLiteral("search_library")) {
        const auto doc = QJsonDocument::fromJson(argumentsJson.toUtf8());
        if (doc.isObject()) {
            const auto obj = doc.object();
            const QString text = obj.value(QStringLiteral("text")).toString().trimmed();
            if (!text.isEmpty()) {
                return QStringLiteral("搜索曲库：%1").arg(text);
            }
        }
        return QStringLiteral("搜索曲库");
    }
    if (name == QStringLiteral("get_lyrics")) {
        return QStringLiteral("读取歌词");
    }
    return name;
}

} // namespace

DeepDiveSession::DeepDiveSession(ai::LlmService &llm, ai::PromptLibrary &prompts,
    const ai::AiConfig &config, DeepDiveTools &tools, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
    , m_prompts(prompts)
    , m_config(config)
    , m_tools(tools)
{
    connect(&m_tools, &DeepDiveTools::executed, this, &DeepDiveSession::onToolExecuted);
}

DeepDiveSession::~DeepDiveSession()
{
    cancel();
}

void DeepDiveSession::reset(const DeepDiveContext &context)
{
    cancel();
    m_history.clear();
    m_context = context;
}

void DeepDiveSession::ask(const QString &question)
{
    if (m_busy) {
        return;
    }
    const QString trimmedQuestion = question.trimmed();
    if (trimmedQuestion.isEmpty()) {
        return;
    }

    m_busy = true;
    m_toolStepsCount = 0;
    m_turnSources.clear();
    m_currentUserQuestion = trimmedQuestion;
    m_pendingToolCalls.clear();
    m_currentToolCallIndex = 0;

    emit turnStarted();

    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("title"), m_context.title);
    vars.insert(QStringLiteral("artist"), m_context.artist);
    vars.insert(QStringLiteral("album"), m_context.album);
    vars.insert(QStringLiteral("year"),
        m_context.year.has_value() ? QString::number(*m_context.year) : QStringLiteral("未知"));
    vars.insert(QStringLiteral("today"), QDate::currentDate().toString(Qt::ISODate));

    const auto renderedRes = m_prompts.render(QStringLiteral("guide/deep_dive"), vars);
    if (!renderedRes.ok()) {
        m_busy = false;
        emit turnFinished(renderedRes.error(), { });
        return;
    }

    m_turnMessages.clear();
    if (!renderedRes.value().system.isEmpty()) {
        m_turnMessages.append(ai::makeMessage(ai::Role::System, renderedRes.value().system));
    }
    m_turnMessages.append(m_history);
    m_turnMessages.append(ai::makeMessage(ai::Role::User, m_currentUserQuestion));

    sendLlmRequest();
}

void DeepDiveSession::cancel()
{
    if (m_currentTask != nullptr) {
        auto task = std::move(m_currentTask);
        task->abort();
    }
    m_pendingToolCalls.clear();
    m_currentToolCallIndex = 0;
    m_turnMessages.clear();
    m_busy = false;
}

bool DeepDiveSession::busy() const
{
    return m_busy;
}

const DeepDiveContext &DeepDiveSession::context() const
{
    return m_context;
}

const QList<ai::ChatMessage> &DeepDiveSession::history() const
{
    return m_history;
}

void DeepDiveSession::sendLlmRequest()
{
    const auto resolved = m_config.resolve(ai::Purpose::Guide);
    if (!resolved.has_value()) {
        m_busy = false;
        emit turnFinished(
            core::Error {
                .code = QString(ai::errc::kNotConfigured),
                .message = QStringLiteral("AI service is not configured for Guide"),
                .detail = QString(),
            },
            { });
        return;
    }

    const bool supportsTools
        = resolved->profile.capabilities.has_value() && resolved->profile.capabilities->tools;
    const bool canUseTools = supportsTools && (m_toolStepsCount < 4);

    ai::ChatRequest req;
    req.messages = m_turnMessages;
    if (canUseTools) {
        req.tools = deepDiveToolSpecs();
    }

    ai::LlmCall call {
        .purpose = ai::Purpose::Guide,
        .request = std::move(req),
        .structured = std::nullopt,
        .stream = true,
        .cachePolicy = ai::CachePolicy::Bypass,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    m_currentDeltaBuffer.clear();
    m_currentTask = m_llm.start(std::move(call));
    auto *taskPtr = m_currentTask.get();

    connect(taskPtr, &ai::LlmTask::delta, this, [this, taskPtr](const QString &text) {
        if (m_currentTask.get() != taskPtr) {
            return;
        }
        m_currentDeltaBuffer.append(text);
        emit delta(text);
    });

    connect(taskPtr, &ai::LlmTask::finished, this, [this, taskPtr]() {
        if (m_currentTask.get() != taskPtr) {
            return;
        }
        const auto task = std::move(m_currentTask);
        onLlmFinished(task->result());
    });
}

void DeepDiveSession::onLlmFinished(const core::Result<ai::LlmResult> &res)
{
    if (!res.ok()) {
        m_busy = false;
        emit turnFinished(res.error(), dedupedSources());
        return;
    }

    const auto &llmRes = res.value();
    const auto &response = llmRes.response;

    if (!response.toolCalls.isEmpty() && m_toolStepsCount < 4) {
        if (!m_currentDeltaBuffer.isEmpty()) {
            emit turnStarted();
        }

        ai::ChatMessage assistantMsg;
        assistantMsg.role = ai::Role::Assistant;
        assistantMsg.content = response.content;
        assistantMsg.toolCalls = response.toolCalls;
        m_turnMessages.append(std::move(assistantMsg));

        ++m_toolStepsCount;
        m_pendingToolCalls = response.toolCalls;
        m_currentToolCallIndex = 0;
        executeNextToolCall();
        return;
    }

    // Final answer
    const QString finalAnswer = response.content;

    m_history.append(ai::makeMessage(ai::Role::User, m_currentUserQuestion));
    m_history.append(ai::makeMessage(ai::Role::Assistant, finalAnswer));
    while (m_history.size() > 20) {
        m_history.removeFirst();
    }

    const auto sources = dedupedSources();
    m_busy = false;
    emit turnFinished(finalAnswer, sources);
}

void DeepDiveSession::executeNextToolCall()
{
    if (!m_busy) {
        return;
    }

    if (m_currentToolCallIndex >= m_pendingToolCalls.size()) {
        m_pendingToolCalls.clear();
        m_currentToolCallIndex = 0;
        sendLlmRequest();
        return;
    }

    const auto &tc = m_pendingToolCalls.at(m_currentToolCallIndex);
    const QString summary = toolSummary(tc.name, tc.arguments);
    emit toolUsed(tc.name, summary);

    m_tools.execute(tc.id, tc.name, tc.arguments, m_context.trackId);
}

void DeepDiveSession::onToolExecuted(
    const QString &callId, const QString &resultJson, const DeepDiveSourceList &sources)
{
    if (!m_busy) {
        return;
    }
    if (m_currentToolCallIndex >= m_pendingToolCalls.size()) {
        return;
    }
    const auto &tc = m_pendingToolCalls.at(m_currentToolCallIndex);
    if (tc.id != callId) {
        return;
    }

    for (const auto &src : sources) {
        m_turnSources.append(src);
    }

    ai::ChatMessage toolMsg;
    toolMsg.role = ai::Role::Tool;
    toolMsg.toolCallId = tc.id;
    toolMsg.content = resultJson;
    m_turnMessages.append(std::move(toolMsg));

    ++m_currentToolCallIndex;
    executeNextToolCall();
}

DeepDiveSourceList DeepDiveSession::dedupedSources() const
{
    DeepDiveSourceList result;
    QSet<QUrl> seenUrls;
    for (const auto &src : m_turnSources) {
        if (src.url.isValid() && !src.url.isEmpty()) {
            if (!seenUrls.contains(src.url)) {
                seenUrls.insert(src.url);
                result.append(src);
            }
        } else {
            result.append(src);
        }
    }
    return result;
}

} // namespace linernotes::guide
