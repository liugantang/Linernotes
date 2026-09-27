// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "StructuredOutput.h"

#include <QJsonDocument>

#include <utility>

namespace linernotes::ai {

namespace {

QString formatSchemaInstruction(const QJsonObject &schema)
{
    const QString schemaText
        = QString::fromUtf8(QJsonDocument(schema).toJson(QJsonDocument::Indented)).trimmed();
    return QStringLiteral("Output only a single JSON value matching the following JSON Schema. Do "
                          "not output any other text or code block markers:\n%1")
        .arg(schemaText);
}

void appendSchemaInstructionToSystemMessage(ChatRequest &request, const QJsonObject &schema)
{
    const QString instruction = formatSchemaInstruction(schema);
    for (auto &msg : request.messages) {
        if (msg.role == Role::System) {
            if (msg.content.isEmpty()) {
                msg.content = instruction;
            } else {
                msg.content += QStringLiteral("\n\n") + instruction;
            }
            return;
        }
    }

    ChatMessage sysMsg;
    sysMsg.role = Role::System;
    sysMsg.content = instruction;
    request.messages.prepend(std::move(sysMsg));
}

} // namespace

StructuredMode chooseStructuredMode(const Capabilities &caps)
{
    if (caps.jsonSchema) {
        return StructuredMode::JsonSchema;
    }
    if (caps.tools) {
        return StructuredMode::Tool;
    }
    if (caps.jsonObject) {
        return StructuredMode::JsonObject;
    }
    return StructuredMode::Prompt;
}

ChatRequest buildStructuredRequest(
    ChatRequest base, const StructuredSpec &spec, StructuredMode mode)
{
    switch (mode) {
    case StructuredMode::JsonSchema:
        base.responseFormat = ResponseFormat::JsonSchema;
        base.schemaName = spec.name;
        base.schema = spec.schema;
        break;
    case StructuredMode::Tool: {
        ToolSpec toolSpec;
        toolSpec.name = spec.name;
        toolSpec.description = spec.description;
        toolSpec.parameters = spec.schema;
        base.tools = { std::move(toolSpec) };
        base.forcedTool = spec.name;
        break;
    }
    case StructuredMode::JsonObject:
        base.responseFormat = ResponseFormat::JsonObject;
        appendSchemaInstructionToSystemMessage(base, spec.schema);
        break;
    case StructuredMode::Prompt:
        appendSchemaInstructionToSystemMessage(base, spec.schema);
        break;
    }
    return base;
}

core::Result<QJsonValue> parseStructuredResponse(const ChatResponse &response,
    const StructuredSpec &spec, const JsonSchema &schema, StructuredMode mode)
{
    QString rawText;
    if (mode == StructuredMode::Tool) {
        bool foundTool = false;
        for (const auto &tc : response.toolCalls) {
            if (tc.name == spec.name) {
                rawText = tc.arguments;
                foundTool = true;
                break;
            }
        }
        if (!foundTool) {
            rawText = response.content;
        }
    } else {
        rawText = response.content;
    }

    const auto jsonRes = extractJson(rawText);
    if (!jsonRes.ok()) {
        return jsonRes.error();
    }

    const auto valRes = schema.validate(jsonRes.value());
    if (!valRes.ok()) {
        return valRes.error();
    }

    return jsonRes.value();
}

ChatRequest buildRepairRequest(ChatRequest sent, const ChatResponse &badResponse,
    const core::Error &error, StructuredMode mode)
{
    ChatMessage assistantMsg;
    assistantMsg.role = Role::Assistant;
    if (mode == StructuredMode::Tool && !badResponse.toolCalls.isEmpty()) {
        assistantMsg.content = badResponse.toolCalls.at(0).arguments;
    } else {
        assistantMsg.content = badResponse.content;
    }

    ChatMessage userMsg;
    userMsg.role = Role::User;
    if (!error.detail.isEmpty()) {
        userMsg.content = QStringLiteral("The previous output is invalid:\n%1\nPlease fix the "
                                         "errors and output only the corrected JSON value.")
                              .arg(error.detail.trimmed());
    } else {
        userMsg.content = QStringLiteral("The previous output is invalid. Please fix the errors "
                                         "and output only the corrected JSON value.");
    }

    sent.messages.append(std::move(assistantMsg));
    sent.messages.append(std::move(userMsg));
    return sent;
}

} // namespace linernotes::ai
