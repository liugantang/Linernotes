// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "ChatTypes.h"

#include "Errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace linernotes::ai {

namespace {

QString roleToString(Role role)
{
    switch (role) {
    case Role::System:
        return QStringLiteral("system");
    case Role::User:
        return QStringLiteral("user");
    case Role::Assistant:
        return QStringLiteral("assistant");
    case Role::Tool:
        return QStringLiteral("tool");
    }
    return QStringLiteral("user");
}

QJsonObject serializeMessage(const ChatMessage &msg)
{
    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), roleToString(msg.role));

    if (msg.role == Role::Tool) {
        msgObj.insert(QStringLiteral("content"), msg.content);
        msgObj.insert(QStringLiteral("tool_call_id"), msg.toolCallId);
    } else if (msg.role == Role::Assistant) {
        if (msg.content.isEmpty() && !msg.toolCalls.isEmpty()) {
            msgObj.insert(QStringLiteral("content"), QJsonValue::Null);
        } else {
            msgObj.insert(QStringLiteral("content"), msg.content);
        }
        if (!msg.toolCalls.isEmpty()) {
            QJsonArray toolCallsArr;
            for (const auto &tc : msg.toolCalls) {
                QJsonObject tcObj;
                tcObj.insert(QStringLiteral("id"), tc.id);
                tcObj.insert(QStringLiteral("type"), QStringLiteral("function"));
                QJsonObject fnObj;
                fnObj.insert(QStringLiteral("name"), tc.name);
                fnObj.insert(QStringLiteral("arguments"), tc.arguments);
                tcObj.insert(QStringLiteral("function"), fnObj);
                toolCallsArr.append(tcObj);
            }
            msgObj.insert(QStringLiteral("tool_calls"), toolCallsArr);
        }
    } else {
        msgObj.insert(QStringLiteral("content"), msg.content);
    }
    return msgObj;
}

QJsonArray serializeTools(const QList<ToolSpec> &tools)
{
    QJsonArray toolsArr;
    for (const auto &tool : tools) {
        QJsonObject toolObj;
        toolObj.insert(QStringLiteral("type"), QStringLiteral("function"));
        QJsonObject fnObj;
        fnObj.insert(QStringLiteral("name"), tool.name);
        if (!tool.description.isEmpty()) {
            fnObj.insert(QStringLiteral("description"), tool.description);
        }
        fnObj.insert(QStringLiteral("parameters"), tool.parameters);
        toolObj.insert(QStringLiteral("function"), fnObj);
        toolsArr.append(toolObj);
    }
    return toolsArr;
}

std::optional<QJsonObject> serializeResponseFormat(const ChatRequest &request)
{
    if (request.responseFormat == ResponseFormat::JsonObject) {
        QJsonObject rfObj;
        rfObj.insert(QStringLiteral("type"), QStringLiteral("json_object"));
        return rfObj;
    }
    if (request.responseFormat == ResponseFormat::JsonSchema) {
        QJsonObject rfObj;
        rfObj.insert(QStringLiteral("type"), QStringLiteral("json_schema"));
        QJsonObject jsObj;
        jsObj.insert(QStringLiteral("name"), request.schemaName);
        jsObj.insert(QStringLiteral("schema"), request.schema);
        jsObj.insert(QStringLiteral("strict"), true);
        rfObj.insert(QStringLiteral("json_schema"), jsObj);
        return rfObj;
    }
    return std::nullopt;
}

} // namespace

QJsonObject toRequestJson(const ChatRequest &request, const QString &model, bool stream)
{
    QJsonObject root;
    root.insert(QStringLiteral("model"), model);

    QJsonArray messagesArr;
    for (const auto &msg : request.messages) {
        messagesArr.append(serializeMessage(msg));
    }
    root.insert(QStringLiteral("messages"), messagesArr);

    if (!request.tools.isEmpty()) {
        root.insert(QStringLiteral("tools"), serializeTools(request.tools));
    }

    if (!request.forcedTool.isEmpty()) {
        QJsonObject choiceObj;
        choiceObj.insert(QStringLiteral("type"), QStringLiteral("function"));
        QJsonObject fnObj;
        fnObj.insert(QStringLiteral("name"), request.forcedTool);
        choiceObj.insert(QStringLiteral("function"), fnObj);
        root.insert(QStringLiteral("tool_choice"), choiceObj);
    }

    if (const auto rf = serializeResponseFormat(request)) {
        root.insert(QStringLiteral("response_format"), *rf);
    }

    if (stream) {
        root.insert(QStringLiteral("stream"), true);
        QJsonObject streamOpts;
        streamOpts.insert(QStringLiteral("include_usage"), true);
        root.insert(QStringLiteral("stream_options"), streamOpts);
    }

    if (request.temperature.has_value()) {
        root.insert(QStringLiteral("temperature"), *request.temperature);
    }

    if (request.maxTokens.has_value()) {
        root.insert(QStringLiteral("max_tokens"), *request.maxTokens);
    }

    return root;
}

core::Result<ChatResponse> parseCompletion(const QByteArray &body)
{
    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return core::Error {
            .code = QString(errc::kBadResponse),
            .message = QStringLiteral("Failed to parse JSON response"),
            .detail = parseErr.errorString(),
        };
    }

    const QJsonObject root = doc.object();
    const QJsonValue choicesVal = root.value(QStringLiteral("choices"));
    if (!choicesVal.isArray()) {
        return core::Error {
            .code = QString(errc::kBadResponse),
            .message = QStringLiteral("Response missing choices array"),
            .detail = QString(),
        };
    }

    const QJsonArray choicesArr = choicesVal.toArray();
    if (choicesArr.isEmpty()) {
        return core::Error {
            .code = QString(errc::kBadResponse),
            .message = QStringLiteral("Response choices array is empty"),
            .detail = QString(),
        };
    }

    const QJsonObject choice0 = choicesArr.at(0).toObject();
    const QString finishReason = choice0.value(QStringLiteral("finish_reason")).toString();

    const QJsonValue messageVal = choice0.value(QStringLiteral("message"));
    if (!messageVal.isObject()) {
        return core::Error {
            .code = QString(errc::kBadResponse),
            .message = QStringLiteral("Response choice missing message object"),
            .detail = QString(),
        };
    }

    const QJsonObject msgObj = messageVal.toObject();
    QString content;
    const QJsonValue contentVal = msgObj.value(QStringLiteral("content"));
    if (contentVal.isString()) {
        content = contentVal.toString();
    }

    QList<ToolCall> toolCalls;
    const QJsonValue toolCallsVal = msgObj.value(QStringLiteral("tool_calls"));
    if (toolCallsVal.isArray()) {
        for (const auto &tcVal : toolCallsVal.toArray()) {
            if (!tcVal.isObject()) {
                continue;
            }
            const QJsonObject tcObj = tcVal.toObject();
            ToolCall tc;
            tc.id = tcObj.value(QStringLiteral("id")).toString();
            const QJsonObject fnObj = tcObj.value(QStringLiteral("function")).toObject();
            tc.name = fnObj.value(QStringLiteral("name")).toString();
            tc.arguments = fnObj.value(QStringLiteral("arguments")).toString();
            toolCalls.append(std::move(tc));
        }
    }

    const QString model = root.value(QStringLiteral("model")).toString();

    TokenUsage usage;
    const QJsonValue usageVal = root.value(QStringLiteral("usage"));
    if (usageVal.isObject()) {
        const QJsonObject uObj = usageVal.toObject();
        usage.promptTokens = uObj.value(QStringLiteral("prompt_tokens")).toInt();
        usage.completionTokens = uObj.value(QStringLiteral("completion_tokens")).toInt();
    }

    return ChatResponse {
        .content = std::move(content),
        .toolCalls = std::move(toolCalls),
        .finishReason = finishReason,
        .model = model,
        .usage = usage,
    };
}

} // namespace linernotes::ai
