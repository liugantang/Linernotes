// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

namespace linernotes::ai {

LlmClient::LlmClient(QNetworkAccessManager &network)
    : m_network(network)
{
}

std::unique_ptr<LlmReply> LlmClient::complete(
    const ServiceConfig &service, const ChatRequest &request)
{
    return sendRequest(service, request, false);
}

std::unique_ptr<LlmReply> LlmClient::stream(
    const ServiceConfig &service, const ChatRequest &request)
{
    return sendRequest(service, request, true);
}

std::unique_ptr<LlmReply> LlmClient::sendRequest(
    const ServiceConfig &service, const ChatRequest &request, bool stream)
{
    QString urlStr = service.baseUrl.toString();
    if (urlStr.endsWith(u'/')) {
        urlStr.chop(1);
    }
    const QUrl requestUrl(urlStr + QStringLiteral("/chat/completions"));

    QNetworkRequest netRequest(requestUrl);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (stream) {
        netRequest.setRawHeader("Accept", "text/event-stream");
    }
    if (!service.apiKey.isEmpty()) {
        netRequest.setRawHeader("Authorization", "Bearer " + service.apiKey.toUtf8());
    }
    if (service.timeoutMs > 0) {
        netRequest.setTransferTimeout(service.timeoutMs);
    }

    const QJsonObject reqJson = toRequestJson(request, service.model, stream);
    const QByteArray body = QJsonDocument(reqJson).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_network.post(netRequest, body);
    return std::unique_ptr<LlmReply>(new LlmReply(reply, service.model, stream));
}

} // namespace linernotes::ai
