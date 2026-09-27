// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "JsonSchema.h"

#include "Errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QLatin1StringView>
#include <QRegularExpression>
#include <QStringList>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace linernotes::ai {

struct JsonSchema::Impl {
    nlohmann::json_schema::json_validator validator { nullptr,
        nlohmann::json_schema::default_string_format_check };
};

namespace {

// 标量不能单独成为 QJsonDocument，统一包一层数组再取出。
nlohmann::json toNlohmann(const QJsonValue &value)
{
    const QByteArray bytes = QJsonDocument(QJsonArray { value }).toJson(QJsonDocument::Compact);
    return nlohmann::json::parse(bytes.toStdString()).at(0);
}

// draft-07 元 schema 校验器：json_validator 本身不检查 schema 是否合法（如 "type": 123）。
const nlohmann::json_schema::json_validator &metaValidator()
{
    static const nlohmann::json_schema::json_validator s_meta(
        nlohmann::json_schema::draft7_schema_builtin, nullptr,
        nlohmann::json_schema::default_string_format_check);
    return s_meta;
}

class SchemaErrorHandler : public nlohmann::json_schema::error_handler {
public:
    SchemaErrorHandler() = default;
    ~SchemaErrorHandler() override = default;
    Q_DISABLE_COPY_MOVE(SchemaErrorHandler)

    void error(const nlohmann::json::json_pointer &ptr, const nlohmann::json & /*instance*/,
        const std::string &message) override
    {
        m_failed = true;
        if (m_errors.size() < 10) {
            const std::string ptrStr = ptr.to_string();
            const QString path
                = ptrStr.empty() ? QStringLiteral("/") : QString::fromStdString(ptrStr);
            m_errors.append(QStringLiteral("%1 : %2").arg(path, QString::fromStdString(message)));
        }
    }

    [[nodiscard]] bool failed() const { return m_failed; }
    [[nodiscard]] QString errorDetail() const { return m_errors.join(QLatin1Char('\n')); }

private:
    bool m_failed = false;
    QStringList m_errors;
};

std::optional<QJsonValue> tryParseJsonValue(const QString &candidate, QJsonParseError *lastError)
{
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    QJsonParseError error { };
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (lastError != nullptr) {
        *lastError = error;
    }

    if (error.error == QJsonParseError::NoError && !doc.isNull()) {
        if (doc.isObject()) {
            return doc.object();
        }
        if (doc.isArray()) {
            return doc.array();
        }
    }
    return std::nullopt;
}

std::optional<QJsonValue> tryExtractFromCodeBlocks(const QString &text, QJsonParseError *lastError)
{
    static const QRegularExpression s_codeBlockRegex(
        QStringLiteral(R"(```(?:[a-zA-Z0-9_-]+)?\s*\r?\n?([\s\S]*?)```)"));

    auto matchIter = s_codeBlockRegex.globalMatch(text);
    while (matchIter.hasNext()) {
        const auto match = matchIter.next();
        const QString blockContent = match.captured(1).trimmed();
        if (!blockContent.isEmpty()) {
            if (const auto val = tryParseJsonValue(blockContent, lastError)) {
                return val;
            }
        }
    }
    return std::nullopt;
}

std::optional<QJsonValue> tryExtractEnclosed(
    const QString &text, QChar openCh, QChar closeCh, QJsonParseError *lastError)
{
    const qsizetype start = text.indexOf(openCh);
    if (start == -1) {
        return std::nullopt;
    }
    const qsizetype end = text.lastIndexOf(closeCh);
    if (end <= start) {
        return std::nullopt;
    }
    const QString candidate = text.mid(start, end - start + 1);
    return tryParseJsonValue(candidate, lastError);
}

std::optional<QJsonValue> tryExtractFromBrackets(const QString &text, QJsonParseError *lastError)
{
    const qsizetype firstBrace = text.indexOf(QLatin1Char('{'));
    const qsizetype firstBracket = text.indexOf(QLatin1Char('['));

    if (firstBrace != -1 && (firstBracket == -1 || firstBrace < firstBracket)) {
        if (const auto val
            = tryExtractEnclosed(text, QLatin1Char('{'), QLatin1Char('}'), lastError)) {
            return val;
        }
        if (firstBracket != -1) {
            return tryExtractEnclosed(text, QLatin1Char('['), QLatin1Char(']'), lastError);
        }
    } else if (firstBracket != -1) {
        if (const auto val
            = tryExtractEnclosed(text, QLatin1Char('['), QLatin1Char(']'), lastError)) {
            return val;
        }
        if (firstBrace != -1) {
            return tryExtractEnclosed(text, QLatin1Char('{'), QLatin1Char('}'), lastError);
        }
    }
    return std::nullopt;
}

} // namespace

core::Result<JsonSchema> JsonSchema::compile(const QJsonObject &schema)
{
    try {
        const nlohmann::json schemaJson = toNlohmann(schema);
        metaValidator().validate(schemaJson);
        auto impl = std::make_shared<Impl>();
        impl->validator.set_root_schema(schemaJson);

        JsonSchema result;
        result.m_impl = std::move(impl);
        return result;
    } catch (const std::exception &ex) {
        return core::Error {
            .code = QString(errc::kSchemaInvalid),
            .message = QStringLiteral("Failed to compile JSON schema"),
            .detail = QString::fromUtf8(ex.what()),
        };
    }
}

core::Result<void> JsonSchema::validate(const QJsonValue &instance) const
{
    if (m_impl == nullptr) {
        return core::Error {
            .code = QString(errc::kSchemaInvalid),
            .message = QStringLiteral("Schema is not initialized"),
            .detail = QString(),
        };
    }

    try {
        const nlohmann::json instanceJson = toNlohmann(instance);
        SchemaErrorHandler handler;
        m_impl->validator.validate(instanceJson, handler);
        if (handler.failed()) {
            return core::Error {
                .code = QString(errc::kSchemaMismatch),
                .message = QStringLiteral("JSON instance does not match schema"),
                .detail = handler.errorDetail(),
            };
        }
        return { };
    } catch (const std::exception &ex) {
        return core::Error {
            .code = QString(errc::kSchemaMismatch),
            .message = QStringLiteral("Validation failed"),
            .detail = QString::fromUtf8(ex.what()),
        };
    }
}

core::Result<QJsonValue> extractJson(const QString &text)
{
    QJsonParseError lastError { };
    const QString trimmed = text.trimmed();
    if (!trimmed.isEmpty()) {
        if (const auto val = tryParseJsonValue(trimmed, &lastError)) {
            return *val;
        }
    }

    if (const auto val = tryExtractFromCodeBlocks(text, &lastError)) {
        return *val;
    }

    if (const auto val = tryExtractFromBrackets(text, &lastError)) {
        return *val;
    }

    return core::Error {
        .code = QString(errc::kBadJson),
        .message = QStringLiteral("Failed to extract valid JSON from text"),
        .detail
        = QStringLiteral("%1 (offset %2)").arg(lastError.errorString()).arg(lastError.offset),
    };
}

} // namespace linernotes::ai
