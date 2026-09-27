// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <core/Result.h>

#include <memory>

namespace linernotes::ai {

/// 已编译的 JSON Schema（draft-07）。值类型，可复制（内部 shared_ptr<const Impl>），
/// 编译后只读，validate() 可多线程并发调用。
class JsonSchema {
public:
    JsonSchema() = default;

    /// schema 本身不合法时返回 errc::kSchemaInvalid，detail 为底层库的错误信息。
    static core::Result<JsonSchema> compile(const QJsonObject &schema);

    /// 不符合时返回 errc::kSchemaMismatch；detail 为多行文本，每行一个错误：
    /// "<JSON Pointer 路径，根为 /> : <错误说明>"，最多列出前 10 条。
    /// 这段 detail 会原样放进重试提示词发给模型，所以要让模型看得懂。
    core::Result<void> validate(const QJsonValue &instance) const;

private:
    struct Impl;
    std::shared_ptr<const Impl> m_impl;
};

/// 从模型回复文本中提取 JSON 值：
/// 1. 整段 trim 后能直接解析 → 返回；
/// 2. 否则找第一个 ```json（或 ```）代码块，解析其内容；
/// 3. 否则取第一个 '{' 或 '[' 到与之配对的最后一个 '}' / ']'
/// 之间的子串解析（不必处理字符串里的括号，找最后一个同类闭括号即可）。 都失败返回
/// errc::kBadJson，detail 为 QJsonParseError 的说明与偏移。
core::Result<QJsonValue> extractJson(const QString &text);

} // namespace linernotes::ai
