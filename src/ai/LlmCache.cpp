// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmCache.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <ai/AiEnumNames.h>
#include <ai/AiLogging.h>
#include <library/Errors.h>

namespace linernotes::ai {

namespace {

QJsonObject toolCallToJson(const ToolCall &tc)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), tc.id);
    obj.insert(QStringLiteral("name"), tc.name);
    obj.insert(QStringLiteral("arguments"), tc.arguments);
    return obj;
}

ToolCall toolCallFromJson(const QJsonObject &obj)
{
    ToolCall tc;
    tc.id = obj.value(QStringLiteral("id")).toString();
    tc.name = obj.value(QStringLiteral("name")).toString();
    tc.arguments = obj.value(QStringLiteral("arguments")).toString();
    return tc;
}

QJsonObject responseToJsonObject(const ChatResponse &response)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("content"), response.content);
    obj.insert(QStringLiteral("finishReason"), response.finishReason);
    obj.insert(QStringLiteral("model"), response.model);

    QJsonArray toolCallsArray;
    for (const auto &tc : response.toolCalls) {
        toolCallsArray.append(toolCallToJson(tc));
    }
    obj.insert(QStringLiteral("toolCalls"), toolCallsArray);

    QJsonObject usageObj;
    usageObj.insert(QStringLiteral("promptTokens"), response.usage.promptTokens);
    usageObj.insert(QStringLiteral("completionTokens"), response.usage.completionTokens);
    obj.insert(QStringLiteral("usage"), usageObj);

    return obj;
}

std::optional<ChatResponse> responseFromJson(const QString &jsonStr)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }

    const QJsonObject obj = doc.object();
    ChatResponse response;
    response.content = obj.value(QStringLiteral("content")).toString();
    response.finishReason = obj.value(QStringLiteral("finishReason")).toString();
    response.model = obj.value(QStringLiteral("model")).toString();

    const QJsonArray toolCallsArray = obj.value(QStringLiteral("toolCalls")).toArray();
    for (const auto val : toolCallsArray) {
        if (val.isObject()) {
            response.toolCalls.append(toolCallFromJson(val.toObject()));
        }
    }

    const QJsonObject usageObj = obj.value(QStringLiteral("usage")).toObject();
    response.usage.promptTokens = usageObj.value(QStringLiteral("promptTokens")).toInt();
    response.usage.completionTokens = usageObj.value(QStringLiteral("completionTokens")).toInt();

    return response;
}

QString structuredModeString(StructuredMode mode)
{
    switch (mode) {
    case StructuredMode::JsonSchema:
        return QStringLiteral("json_schema");
    case StructuredMode::Tool:
        return QStringLiteral("tool");
    case StructuredMode::JsonObject:
        return QStringLiteral("json_object");
    case StructuredMode::Prompt:
        return QStringLiteral("prompt");
    }
    return { };
}

} // namespace

LlmCache::LlmCache(library::Database &db, const core::Clock &clock)
    : m_db(db)
    , m_clock(clock)
{
}

std::optional<ChatResponse> LlmCache::get(const QString &key)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        qCWarning(lcAi) << "LlmCache::get failed to get db connection:" << connRes.error().message;
        return std::nullopt;
    }

    QSqlQuery query(connRes.value());
    query.prepare(QStringLiteral("SELECT response, expires_at FROM llm_cache WHERE key = ?"));
    query.addBindValue(key);

    if (!query.exec()) {
        qCWarning(lcAi) << "LlmCache::get query failed:" << query.lastError().text();
        return std::nullopt;
    }

    if (!query.next()) {
        return std::nullopt;
    }

    const QVariant expiresVal = query.value(1);
    if (!expiresVal.isNull()) {
        const qint64 expiresAt = expiresVal.toLongLong();
        if (expiresAt <= m_clock.nowMs()) {
            return std::nullopt;
        }
    }

    const QString responseText = query.value(0).toString();
    auto respOpt = responseFromJson(responseText);
    if (!respOpt.has_value()) {
        qCWarning(lcAi) << "LlmCache::get failed to parse cached response JSON for key:" << key;
        return std::nullopt;
    }

    QSqlQuery updateQuery(connRes.value());
    updateQuery.prepare(QStringLiteral("UPDATE llm_cache SET hits = hits + 1 WHERE key = ?"));
    updateQuery.addBindValue(key);
    if (!updateQuery.exec()) {
        qCWarning(lcAi) << "LlmCache::get failed to increment hits for key:" << key
                        << updateQuery.lastError().text();
    }

    return respOpt;
}

core::Result<void> LlmCache::put(const QString &key, const QString &model, Purpose purpose,
    const ChatResponse &response, std::optional<qint64> ttlMs)
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    const qint64 nowMs = m_clock.nowMs();
    const QVariant expiresAt = ttlMs.has_value() ? QVariant::fromValue(nowMs + *ttlMs)
                                                 : QVariant(QMetaType::fromType<qint64>());
    const QString responseText = QString::fromUtf8(
        QJsonDocument(responseToJsonObject(response)).toJson(QJsonDocument::Compact));
    const QString purpStr = purposeName(purpose);

    QSqlQuery query(connRes.value());
    query.prepare(QStringLiteral(
        "INSERT INTO llm_cache (key, model, purpose, response, created_at, expires_at, hits) "
        "VALUES (?, ?, ?, ?, ?, ?, 0) "
        "ON CONFLICT(key) DO UPDATE SET "
        "model = excluded.model, "
        "purpose = excluded.purpose, "
        "response = excluded.response, "
        "created_at = excluded.created_at, "
        "expires_at = excluded.expires_at, "
        "hits = 0"));
    query.addBindValue(key);
    query.addBindValue(model);
    query.addBindValue(purpStr);
    query.addBindValue(responseText);
    query.addBindValue(nowMs);
    query.addBindValue(expiresAt);

    if (!query.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = query.lastError().text(),
            .detail = query.lastQuery(),
        };
    }

    return { };
}

core::Result<int> LlmCache::purgeExpired()
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    const qint64 nowMs = m_clock.nowMs();
    QSqlQuery query(connRes.value());
    query.prepare(
        QStringLiteral("DELETE FROM llm_cache WHERE expires_at IS NOT NULL AND expires_at <= ?"));
    query.addBindValue(nowMs);

    if (!query.exec()) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = query.lastError().text(),
            .detail = query.lastQuery(),
        };
    }

    return query.numRowsAffected();
}

core::Result<void> LlmCache::clear()
{
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return connRes.error();
    }

    QSqlQuery query(connRes.value());
    if (!query.exec(QStringLiteral("DELETE FROM llm_cache"))) {
        return core::Error {
            .code = QString(library::errc::kDbQuery),
            .message = query.lastError().text(),
            .detail = query.lastQuery(),
        };
    }

    return { };
}

QString LlmCache::makeKey(const QUrl &baseUrl, const QString &model, const ChatRequest &request,
    const std::optional<StructuredSpec> &spec, StructuredMode mode)
{
    QString normalized;
    normalized += baseUrl.toString();
    normalized += QLatin1Char('\n');
    normalized += model;
    normalized += QLatin1Char('\n');

    const QJsonObject reqObj = toRequestJson(request, model, false);
    normalized += QString::fromUtf8(QJsonDocument(reqObj).toJson(QJsonDocument::Compact));
    normalized += QLatin1Char('\n');

    if (spec.has_value()) {
        normalized += spec->name;
        normalized += QString::fromUtf8(QJsonDocument(spec->schema).toJson(QJsonDocument::Compact));
        normalized += structuredModeString(mode);
    }

    const QByteArray utf8 = normalized.toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(utf8, QCryptographicHash::Sha256).toHex());
}

} // namespace linernotes::ai
