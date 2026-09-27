// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <ai/ChatTypes.h>
#include <ai/LlmReply.h>

#include <memory>

class QNetworkAccessManager;

namespace linernotes::ai {

class LlmClient {
public:
    explicit LlmClient(QNetworkAccessManager &network);

    std::unique_ptr<LlmReply> complete(const ServiceConfig &service, const ChatRequest &request);
    std::unique_ptr<LlmReply> stream(const ServiceConfig &service, const ChatRequest &request);

private:
    std::unique_ptr<LlmReply> sendRequest(
        const ServiceConfig &service, const ChatRequest &request, bool stream);

    QNetworkAccessManager &m_network;
};

} // namespace linernotes::ai
