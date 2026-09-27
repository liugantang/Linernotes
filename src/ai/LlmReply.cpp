// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmReply.h"

#include "AiLogging.h"
#include "Errors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>

namespace linernotes::ai {

void DeleteLater::operator()(QObject *obj) const
{
    if (obj != nullptr) {
        obj->deleteLater();
    }
}

namespace {

struct ErrorMapping {
    QString code;
    QString message;
};

ErrorMapping mapNetworkOrHttpError(int status, QNetworkReply::NetworkError netErr, bool aborted)
{
    if (status == 401 || status == 403) {
        return { .code = QString(errc::kAuth), .message = QStringLiteral("Authentication failed") };
    }
    if (status == 429) {
        return { .code = QString(errc::kRateLimited), .message = QStringLiteral("Rate limited") };
    }
    if (status >= 400 && status < 600) {
        return { .code = QString(errc::kHttp),
            .message = QStringLiteral("HTTP error %1").arg(status) };
    }
    if (netErr == QNetworkReply::TimeoutError) {
        return { .code = QString(errc::kTimeout), .message = QStringLiteral("Request timed out") };
    }
    if (netErr == QNetworkReply::OperationCanceledError && aborted) {
        return { .code = QString(errc::kAborted), .message = QStringLiteral("Request aborted") };
    }
    return { .code = QString(errc::kNetwork), .message = QStringLiteral("Network error") };
}

QString extractErrorMessage(const QByteArray &body)
{
    if (body.isEmpty()) {
        return { };
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            const QJsonValue errVal = obj.value(QStringLiteral("error"));
            if (errVal.isObject()) {
                const QString msg = errVal.toObject().value(QStringLiteral("message")).toString();
                if (!msg.isEmpty()) {
                    return msg;
                }
            } else if (errVal.isString()) {
                return errVal.toString();
            }
        }
        if (obj.contains(QStringLiteral("message"))) {
            const QString msg = obj.value(QStringLiteral("message")).toString();
            if (!msg.isEmpty()) {
                return msg;
            }
        }
    }

    return QString::fromUtf8(body.left(200));
}

void accumulateToolCalls(const QJsonArray &tcArr, QList<ToolCall> &toolCalls)
{
    for (const auto &tcVal : tcArr) {
        if (!tcVal.isObject()) {
            continue;
        }
        const QJsonObject tcObj = tcVal.toObject();
        const int idx = tcObj.value(QStringLiteral("index")).toInt();
        if (idx < 0) {
            continue;
        }
        while (toolCalls.size() <= idx) {
            toolCalls.append(ToolCall());
        }

        Q_ASSERT(idx < toolCalls.size());
        ToolCall &tc = *(toolCalls.begin() + idx);
        if (tcObj.contains(QStringLiteral("id"))) {
            tc.id += tcObj.value(QStringLiteral("id")).toString();
        }
        if (tcObj.contains(QStringLiteral("function"))
            && tcObj.value(QStringLiteral("function")).isObject()) {
            const QJsonObject fnObj = tcObj.value(QStringLiteral("function")).toObject();
            if (fnObj.contains(QStringLiteral("name"))) {
                tc.name += fnObj.value(QStringLiteral("name")).toString();
            }
            if (fnObj.contains(QStringLiteral("arguments"))) {
                tc.arguments += fnObj.value(QStringLiteral("arguments")).toString();
            }
        }
    }
}

void processStreamChunk(const QJsonObject &obj, ChatResponse &accumulated, QString &deltaTextOut)
{
    if (obj.contains(QStringLiteral("model"))) {
        accumulated.model = obj.value(QStringLiteral("model")).toString();
    }

    if (obj.contains(QStringLiteral("usage")) && obj.value(QStringLiteral("usage")).isObject()) {
        const QJsonObject uObj = obj.value(QStringLiteral("usage")).toObject();
        accumulated.usage.promptTokens = uObj.value(QStringLiteral("prompt_tokens")).toInt();
        accumulated.usage.completionTokens
            = uObj.value(QStringLiteral("completion_tokens")).toInt();
    }

    if (!obj.contains(QStringLiteral("choices"))
        || !obj.value(QStringLiteral("choices")).isArray()) {
        return;
    }

    const QJsonArray choicesArr = obj.value(QStringLiteral("choices")).toArray();
    if (choicesArr.isEmpty()) {
        return;
    }

    const QJsonObject choiceObj = choicesArr.at(0).toObject();
    if (choiceObj.contains(QStringLiteral("finish_reason"))
        && !choiceObj.value(QStringLiteral("finish_reason")).isNull()) {
        accumulated.finishReason = choiceObj.value(QStringLiteral("finish_reason")).toString();
    }

    if (!choiceObj.contains(QStringLiteral("delta"))
        || !choiceObj.value(QStringLiteral("delta")).isObject()) {
        return;
    }

    const QJsonObject deltaObj = choiceObj.value(QStringLiteral("delta")).toObject();
    if (deltaObj.contains(QStringLiteral("content"))
        && deltaObj.value(QStringLiteral("content")).isString()) {
        const QString text = deltaObj.value(QStringLiteral("content")).toString();
        accumulated.content += text;
        deltaTextOut = text;
    }

    if (deltaObj.contains(QStringLiteral("tool_calls"))
        && deltaObj.value(QStringLiteral("tool_calls")).isArray()) {
        accumulateToolCalls(
            deltaObj.value(QStringLiteral("tool_calls")).toArray(), accumulated.toolCalls);
    }
}

} // namespace

LlmReply::LlmReply(QNetworkReply *reply, QString model, bool stream, QObject *parent)
    : QObject(parent)
    , m_networkReply(reply)
    , m_model(std::move(model))
    , m_stream(stream)
{
    m_timer.start();

    if (m_networkReply != nullptr) {
        connect(m_networkReply.get(), &QNetworkReply::readyRead, this, &LlmReply::onReadyRead);
        connect(m_networkReply.get(), &QNetworkReply::finished, this, &LlmReply::onNetworkFinished);
    }
}

LlmReply::~LlmReply()
{
    if (!m_finished) {
        releaseNetworkReply(true);
    }
}

void LlmReply::releaseNetworkReply(bool abortTransfer)
{
    if (m_networkReply != nullptr) {
        m_networkReply->disconnect(this);
        if (abortTransfer) {
            m_networkReply->abort();
        }
        m_networkReply = nullptr;
    }
}

bool LlmReply::isFinished() const
{
    return m_finished;
}

const core::Result<ChatResponse> &LlmReply::result() const
{
    Q_ASSERT_X(m_finished, "LlmReply::result", "Called result() before reply finished");
    return m_result;
}

int LlmReply::httpStatus() const
{
    return m_httpStatus;
}

std::optional<qint64> LlmReply::retryAfterMs() const
{
    return m_retryAfterMs;
}

qint64 LlmReply::elapsedMs() const
{
    if (m_finished) {
        return m_elapsedMs;
    }
    return m_timer.elapsed();
}

void LlmReply::abort()
{
    if (m_finished) {
        return;
    }

    m_aborted = true;
    m_elapsedMs = m_timer.elapsed();

    releaseNetworkReply(true);

    finishWithError(core::Error {
        .code = QString(errc::kAborted),
        .message = QStringLiteral("Request aborted"),
        .detail = QStringLiteral("Aborted by user"),
    });
}

void LlmReply::extractResponseDetails()
{
    if (m_networkReply == nullptr) {
        return;
    }
    const QVariant statusVar = m_networkReply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusVar.isValid()) {
        m_httpStatus = statusVar.toInt();
    }
    const QByteArray retryHeader = m_networkReply->rawHeader("Retry-After");
    if (!retryHeader.isEmpty()) {
        bool ok = false;
        const qint64 sec = retryHeader.trimmed().toLongLong(&ok);
        if (ok && sec >= 0) {
            m_retryAfterMs = sec * 1000;
        }
    }
}

void LlmReply::onReadyRead()
{
    if (m_finished || m_networkReply == nullptr) {
        return;
    }

    extractResponseDetails();

    const QNetworkReply::NetworkError netErr = m_networkReply->error();
    if (!m_stream) {
        m_responseBody.append(m_networkReply->readAll());
        return;
    }

    if (m_httpStatus >= 400 || (m_httpStatus == 0 && netErr != QNetworkReply::NoError)) {
        m_responseBody.append(m_networkReply->readAll());
        return;
    }

    const QByteArray chunk = m_networkReply->readAll();
    const QList<SseEvent> events = m_sseParser.feed(chunk);

    for (const auto &event : events) {
        const QByteArray trimmedData = event.data.trimmed();
        if (trimmedData == "[DONE]") {
            m_receivedDone = true;
            continue;
        }

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(event.data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            m_hadParseError = true;
            m_parseErrorMsg = parseErr.errorString();
            continue;
        }

        QString deltaText;
        processStreamChunk(doc.object(), m_accumulatedResponse, deltaText);
        if (!deltaText.isEmpty()) {
            emit delta(deltaText);
        }
    }
}

void LlmReply::onNetworkFinished()
{
    if (m_finished) {
        return;
    }

    onReadyRead();

    m_elapsedMs = m_timer.elapsed();
    extractResponseDetails();

    const QNetworkReply::NetworkError netErr
        = (m_networkReply != nullptr) ? m_networkReply->error() : QNetworkReply::NoError;
    const int status = m_httpStatus;

    if (m_stream) {
        if (status >= 400 || (status == 0 && netErr != QNetworkReply::NoError)) {
            finishWithNetworkOrHttpError();
            return;
        }

        if (m_hadParseError) {
            finishWithError(core::Error {
                .code = QString(errc::kBadResponse),
                .message = QStringLiteral("Malformed SSE chunk JSON"),
                .detail = m_parseErrorMsg,
            });
            return;
        }

        if (!m_receivedDone) {
            finishWithError(core::Error {
                .code = QString(errc::kBadResponse),
                .message = QStringLiteral("Stream disconnected before [DONE]"),
                .detail = QStringLiteral("Response stream was truncated"),
            });
            return;
        }

        if (m_accumulatedResponse.model.isEmpty()) {
            m_accumulatedResponse.model = m_model;
        }
        finishWithSuccess(m_accumulatedResponse);
        return;
    }

    if (status >= 400 || (status == 0 && netErr != QNetworkReply::NoError)) {
        finishWithNetworkOrHttpError();
        return;
    }

    auto parseRes = parseCompletion(m_responseBody);
    if (!parseRes.ok()) {
        finishWithError(parseRes.error());
        return;
    }

    finishWithSuccess(parseRes.value());
}

void LlmReply::finishWithNetworkOrHttpError()
{
    const QNetworkReply::NetworkError netErr
        = (m_networkReply != nullptr) ? m_networkReply->error() : QNetworkReply::NoError;
    const int status = m_httpStatus;

    const auto [code, message] = mapNetworkOrHttpError(status, netErr, m_aborted);

    QString errorDetail = extractErrorMessage(m_responseBody);
    if (errorDetail.isEmpty() && m_networkReply != nullptr) {
        errorDetail = m_networkReply->errorString();
    }

    QString detail;
    if (status > 0) {
        detail = QStringLiteral("HTTP %1: %2").arg(QString::number(status), errorDetail);
    } else {
        detail = errorDetail;
    }

    finishWithError(core::Error {
        .code = code,
        .message = message,
        .detail = detail,
    });
}

void LlmReply::finishWithError(core::Error error)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_result = std::move(error);

    releaseNetworkReply(false);

    qCInfo(lcAi, "LLM request finished with error: model=%s elapsed=%lldms status=%d code=%s",
        qPrintable(m_model), m_elapsedMs, m_httpStatus, qPrintable(m_result.error().code));

    emit finished();
}

void LlmReply::finishWithSuccess(ChatResponse response)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    const int promptTokens = response.usage.promptTokens;
    const int completionTokens = response.usage.completionTokens;
    const QString respModel = response.model.isEmpty() ? m_model : response.model;

    m_result = std::move(response);

    releaseNetworkReply(false);

    qCInfo(lcAi,
        "LLM request finished: model=%s elapsed=%lldms status=%d prompt_tokens=%d "
        "completion_tokens=%d",
        qPrintable(respModel), m_elapsedMs, m_httpStatus, promptTokens, completionTokens);

    emit finished();
}

} // namespace linernotes::ai
