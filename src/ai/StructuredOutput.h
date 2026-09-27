// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <ai/ChatTypes.h>
#include <ai/JsonSchema.h>
#include <core/Result.h>

#include <cstdint>

namespace linernotes::ai {

struct Capabilities {
    bool jsonSchema = false; // response_format: json_schema
    bool tools = false; // 强制 tool_choice 的函数调用
    bool jsonObject = false; // response_format: json_object
    bool operator==(const Capabilities &) const = default;
};

enum class StructuredMode : std::uint8_t { JsonSchema, Tool, JsonObject, Prompt };

/// 优先级：JsonSchema > Tool > JsonObject > Prompt。
StructuredMode chooseStructuredMode(const Capabilities &caps);

struct StructuredSpec {
    QString name; // 仅字母数字下划线，用作 schema 名 / 工具名
    QString description; // 给模型看的一句话说明
    QJsonObject schema; // JSON Schema（draft-07 子集）
};

/// 在 base（已含 system/user 消息）基础上加入结构化约束：
/// - JsonSchema：responseFormat=JsonSchema，schemaName/schema 取自 spec
/// - Tool：tools=[{spec.name, spec.description, spec.schema}]，forcedTool=spec.name
/// - JsonObject：responseFormat=JsonObject，并追加格式说明（见下）
/// - Prompt：只追加格式说明
/// 格式说明：追加到第一条 System 消息末尾（没有则在最前面插入一条 System 消息），内容为
/// “只输出一个符合下列 JSON Schema 的 JSON 值，不要输出任何其他文字或代码块标记” + 缩进后的 schema
/// 文本。 JsonObject 也要加，因为 json_object 模式只保证是
/// JSON，不保证结构。文字用英文写（对各模型最稳）。
ChatRequest buildStructuredRequest(
    ChatRequest base, const StructuredSpec &spec, StructuredMode mode);

/// 从响应中取出 JSON 并按 schema 校验：
/// Tool 模式取第一个 name==spec.name 的 toolCall 的 arguments（没有时退回到 content，
/// 有的本地服务会忽略 tool_choice 直接写在 content 里）；其他模式取 content。
/// 用 extractJson 提取，用 schema.validate 校验。
/// 失败返回 errc::kBadJson / errc::kSchemaMismatch（沿用 extractJson / validate 的 Error）。
core::Result<QJsonValue> parseStructuredResponse(const ChatResponse &response,
    const StructuredSpec &spec, const JsonSchema &schema, StructuredMode mode);

/// 第一次解析失败后的修复请求：在 sent 的消息后追加
/// - 一条 Assistant 消息：模型上次的原始输出（Tool 模式下为 tool call 的 arguments 文本，放在
/// content 中，
///   不要带 toolCalls，避免还要配 Tool 结果消息）
/// - 一条 User 消息：说明上次输出无效，附上 error.detail，要求只输出修正后的 JSON（英文）
/// 其余字段（tools、forcedTool、responseFormat…）保持不变。
ChatRequest buildRepairRequest(ChatRequest sent, const ChatResponse &badResponse,
    const core::Error &error, StructuredMode mode);

} // namespace linernotes::ai
