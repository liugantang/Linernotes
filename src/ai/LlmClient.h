// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QStringList>

#include <ai/ChatTypes.h>
#include <ai/LlmReply.h>
#include <core/Result.h>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QObject;

namespace linernotes::ai {

using ModelListCallback = std::function<void(const core::Result<QStringList> &)>;

class LlmClient {
public:
    explicit LlmClient(QNetworkAccessManager &network);

    std::unique_ptr<LlmReply> complete(const ServiceConfig &service, const ChatRequest &request);
    std::unique_ptr<LlmReply> stream(const ServiceConfig &service, const ChatRequest &request);

    void listModels(const ServiceConfig &service, QObject *context, ModelListCallback callback);

private:
    std::unique_ptr<LlmReply> sendRequest(
        const ServiceConfig &service, const ChatRequest &request, bool stream);

    QNetworkAccessManager &m_network;
};

} // namespace linernotes::ai
