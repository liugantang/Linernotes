// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <core/Result.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace linernotes::ai {

struct ServiceConfig {
    QUrl baseUrl; // 如 https://api.deepseek.com/v1 或 http://localhost:11434/v1；请求发往 baseUrl +
                  // "/chat/completions"（处理好结尾斜杠）
    QString apiKey; // 可为空（本地服务）；非空时加 "Authorization: Bearer <key>"
    QString model;
    int timeoutMs = 60000; // 传输空闲超时，用 QNetworkRequest::setTransferTimeout
};

enum class Role : std::uint8_t { System, User, Assistant, Tool };

struct ToolCall {
    QString id;
    QString name;
    QString arguments; // 模型给出的 JSON 文本，原样保留，不在此解析
    bool operator==(const ToolCall &) const = default;
};

struct ChatMessage {
    Role role = Role::User;
    QString content;
    QList<ToolCall> toolCalls; // 仅 Assistant
    QString toolCallId; // 仅 Tool
    bool operator==(const ChatMessage &) const = default;
};

/// 只有角色与正文的消息（最常见的 System / User / Assistant 消息）。
inline ChatMessage makeMessage(Role role, QString content)
{
    ChatMessage message;
    message.role = role;
    message.content = std::move(content);
    return message;
}

struct ToolSpec {
    QString name;
    QString description;
    QJsonObject parameters; // JSON Schema
};

enum class ResponseFormat : std::uint8_t { Text, JsonObject, JsonSchema };

struct ChatRequest {
    QList<ChatMessage> messages;
    QList<ToolSpec> tools;
    QString forcedTool; // 非空时 tool_choice = {"type":"function","function":{"name":...}}
    ResponseFormat responseFormat = ResponseFormat::Text;
    QString schemaName; // ResponseFormat::JsonSchema 时使用
    QJsonObject schema; // ResponseFormat::JsonSchema 时使用（strict: true）
    std::optional<double> temperature;
    std::optional<int> maxTokens;
};

struct TokenUsage {
    int promptTokens = 0;
    int completionTokens = 0;
    bool operator==(const TokenUsage &) const = default;
};

struct ChatResponse {
    QString content;
    QList<ToolCall> toolCalls;
    QString finishReason;
    QString model; // 服务端返回的模型名
    TokenUsage usage; // 服务端未返回时为 0
    bool operator==(const ChatResponse &) const = default;
};

/// 生成请求体。stream=true 时加 "stream": true 与 "stream_options": {"include_usage": true}。
QJsonObject toRequestJson(const ChatRequest &request, const QString &model, bool stream);
/// 解析非流式响应体（choices[0].message、finish_reason、usage、model）。
core::Result<ChatResponse> parseCompletion(const QByteArray &body);

} // namespace linernotes::ai
