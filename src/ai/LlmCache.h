// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/StructuredOutput.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <library/Database.h>

#include <optional>

namespace linernotes::ai {

/// llm_cache 表的读写。只缓存成功的响应。线程：在调用线程使用 Database 的本线程连接。
class LlmCache {
public:
    LlmCache(library::Database &db, const core::Clock &clock);
    ~LlmCache() = default;
    Q_DISABLE_COPY_MOVE(LlmCache)

    /// 未命中或已过期返回 nullopt；命中时 hits + 1。数据库错误按未命中处理并 qCWarning。
    std::optional<ChatResponse> get(const QString &key);
    /// ttlMs 为空表示永不过期。同 key 覆盖。
    core::Result<void> put(const QString &key, const QString &model, Purpose purpose,
        const ChatResponse &response, std::optional<qint64> ttlMs);
    /// 删除所有已过期条目，返回删除条数。
    core::Result<int> purgeExpired();
    /// 清空缓存（设置页“清除缓存”用）。
    core::Result<void> clear();

    /// 缓存键：SHA-256(十六进制) of 规范化文本 = baseUrl + '\n' + model + '\n' + 请求 JSON
    /// （toRequestJson(request, model, /*stream=*/false)，QJsonDocument::Compact；QJsonObject
    /// 键本身有序，足够稳定）
    /// + '\n' + 结构化部分（spec 为空时为空串；否则 spec.name + schema 的 Compact JSON + 模式名）。
    static QString makeKey(const QUrl &baseUrl, const QString &model, const ChatRequest &request,
        const std::optional<StructuredSpec> &spec, StructuredMode mode);

private:
    library::Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::ai
