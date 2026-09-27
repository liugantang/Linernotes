// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/LlmClient.h>
#include <ai/LlmReply.h>
#include <common/FakeLlmServer.h>

using linernotes::ai::ChatMessage;
using linernotes::ai::ChatRequest;
using linernotes::ai::ChatResponse;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmReply;
using linernotes::ai::ResponseFormat;
using linernotes::ai::Role;
using linernotes::ai::ServiceConfig;
using linernotes::ai::ToolSpec;
using linernotes::test::FakeLlmServer;

namespace {

ChatMessage userMessage(const QString &text)
{
    ChatMessage m;
    m.role = Role::User;
    m.content = text;
    return m;
}

class TstLlmClient : public QObject {
    Q_OBJECT

private slots:
    void nonStreamingRequestAndResponse();
    void emptyApiKeyNoAuthorizationHeader();
    void streamingContentAndUsage();
    void streamingToolCallsConcat();
    void streamingTruncatedWithoutDoneReturnsBadResponse();
    void errorMappingAuth();
    void errorMappingRateLimitedWithRetryAfter();
    void errorMappingHttpServerError();
    void errorMappingBadResponseNonJson();
    void errorMappingTimeout();
    void errorMappingNetworkConnectionRefused();
    void abortInFlight();
};

void TstLlmClient::nonStreamingRequestAndResponse()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    QJsonObject respJson;
    respJson.insert(QStringLiteral("model"), QStringLiteral("gpt-4o"));

    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msgObj.insert(QStringLiteral("content"), QStringLiteral("Hello world"));

    QJsonArray toolCallsArr;
    QJsonObject tcObj;
    tcObj.insert(QStringLiteral("id"), QStringLiteral("call_1"));
    QJsonObject fnObj;
    fnObj.insert(QStringLiteral("name"), QStringLiteral("get_weather"));
    fnObj.insert(QStringLiteral("arguments"), QStringLiteral("{\"city\":\"Tokyo\"}"));
    tcObj.insert(QStringLiteral("function"), fnObj);
    toolCallsArr.append(tcObj);
    msgObj.insert(QStringLiteral("tool_calls"), toolCallsArr);

    QJsonObject choice0;
    choice0.insert(QStringLiteral("finish_reason"), QStringLiteral("tool_calls"));
    choice0.insert(QStringLiteral("message"), msgObj);

    QJsonArray choicesArr;
    choicesArr.append(choice0);
    respJson.insert(QStringLiteral("choices"), choicesArr);

    QJsonObject usageObj;
    usageObj.insert(QStringLiteral("prompt_tokens"), 15);
    usageObj.insert(QStringLiteral("completion_tokens"), 8);
    respJson.insert(QStringLiteral("usage"), usageObj);

    server.enqueue(FakeLlmServer::json(respJson, 200));

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("sk-test-key"),
        .model = QStringLiteral("gpt-4o"),
        .timeoutMs = 5000,
    };

    ChatRequest req;
    req.messages.append(userMessage(QStringLiteral("What's the weather?")));
    ToolSpec ts;
    ts.name = QStringLiteral("get_weather");
    ts.description = QStringLiteral("Get weather for city");
    ts.parameters = QJsonObject { { QStringLiteral("type"), QStringLiteral("object") } };
    req.tools.append(ts);
    req.forcedTool = QStringLiteral("get_weather");
    req.responseFormat = ResponseFormat::JsonSchema;
    req.schemaName = QStringLiteral("WeatherResponse");
    req.schema = QJsonObject { { QStringLiteral("type"), QStringLiteral("object") } };
    req.temperature = 0.7;
    req.maxTokens = 100;

    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QCOMPARE(reply->httpStatus(), 200);
    QVERIFY(reply->result().ok());

    const ChatResponse &resp = reply->result().value();
    QCOMPARE(resp.content, QStringLiteral("Hello world"));
    QCOMPARE(resp.model, QStringLiteral("gpt-4o"));
    QCOMPARE(resp.finishReason, QStringLiteral("tool_calls"));
    QCOMPARE(resp.usage.promptTokens, 15);
    QCOMPARE(resp.usage.completionTokens, 8);
    QCOMPARE(resp.toolCalls.size(), 1);
    QCOMPARE(resp.toolCalls.at(0).id, QStringLiteral("call_1"));
    QCOMPARE(resp.toolCalls.at(0).name, QStringLiteral("get_weather"));
    QCOMPARE(resp.toolCalls.at(0).arguments, QStringLiteral("{\"city\":\"Tokyo\"}"));

    // Verify request received by server
    const auto requests = server.requests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.at(0).method, QByteArray("POST"));
    QCOMPARE(requests.at(0).path, QByteArray("/v1/chat/completions"));
    QCOMPARE(requests.at(0).headers.value("authorization"), QByteArray("Bearer sk-test-key"));
    QCOMPARE(requests.at(0).headers.value("content-type"), QByteArray("application/json"));

    const QJsonDocument reqDoc = QJsonDocument::fromJson(requests.at(0).body);
    QVERIFY(reqDoc.isObject());
    const QJsonObject sentJson = reqDoc.object();
    QCOMPARE(sentJson.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o"));
    QVERIFY(sentJson.contains(QStringLiteral("tools")));
    QVERIFY(sentJson.contains(QStringLiteral("tool_choice")));
    QVERIFY(sentJson.contains(QStringLiteral("response_format")));
    QCOMPARE(sentJson.value(QStringLiteral("temperature")).toDouble(), 0.7);
    QCOMPARE(sentJson.value(QStringLiteral("max_tokens")).toInt(), 100);
}

void TstLlmClient::emptyApiKeyNoAuthorizationHeader()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    QJsonObject respJson;
    respJson.insert(QStringLiteral("choices"),
        QJsonArray { QJsonObject { { QStringLiteral("finish_reason"), QStringLiteral("stop") },
            { QStringLiteral("message"),
                QJsonObject { { QStringLiteral("content"), QStringLiteral("ok") } } } } });
    server.enqueue(FakeLlmServer::json(respJson));

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QString(),
        .model = QStringLiteral("local-model"),
    };

    ChatRequest req;
    req.messages.append(userMessage(QStringLiteral("hi")));

    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QVERIFY(reply->result().ok());

    const auto requests = server.requests();
    QCOMPARE(requests.size(), 1);
    QVERIFY(!requests.at(0).headers.contains("authorization"));
}

void TstLlmClient::streamingContentAndUsage()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    const QList<QByteArray> payloads
        = { QByteArray(R"({"choices":[{"delta":{"content":"Hello "}}],"model":"gpt-4o"})"),
              QByteArray(R"({"choices":[{"delta":{"content":"world!"}}]})"),
              QByteArray(R"({"choices":[],"usage":{"prompt_tokens":8,"completion_tokens":4}})") };
    server.enqueue(FakeLlmServer::sse(payloads));

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("key"),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    req.messages.append(userMessage(QStringLiteral("hi")));

    auto reply = client.stream(config, req);
    QSignalSpy deltaSpy(reply.get(), &LlmReply::delta);
    QSignalSpy finishSpy(reply.get(), &LlmReply::finished);

    QVERIFY(finishSpy.wait(2000));
    QCOMPARE(deltaSpy.count(), 2);
    QCOMPARE(deltaSpy.at(0).at(0).toString(), QStringLiteral("Hello "));
    QCOMPARE(deltaSpy.at(1).at(0).toString(), QStringLiteral("world!"));

    QVERIFY(reply->result().ok());
    const ChatResponse &resp = reply->result().value();
    QCOMPARE(resp.content, QStringLiteral("Hello world!"));
    QCOMPARE(resp.model, QStringLiteral("gpt-4o"));
    QCOMPARE(resp.usage.promptTokens, 8);
    QCOMPARE(resp.usage.completionTokens, 4);
}

void TstLlmClient::streamingToolCallsConcat()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    const QList<QByteArray> payloads = {
        QByteArray(
            R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_123","function":{"name":"search","arguments":"{\"q\":"}}]}}]})"),
        QByteArray(
            R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\"beethoven\"}"}}]}}]})"),
        QByteArray(R"({"choices":[{"delta":{},"finish_reason":"tool_calls"}]})")
    };
    server.enqueue(FakeLlmServer::sse(payloads));

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("key"),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    req.messages.append(userMessage(QStringLiteral("search")));

    auto reply = client.stream(config, req);
    QSignalSpy finishSpy(reply.get(), &LlmReply::finished);

    QVERIFY(finishSpy.wait(2000));
    QVERIFY(reply->result().ok());
    const ChatResponse &resp = reply->result().value();
    QCOMPARE(resp.finishReason, QStringLiteral("tool_calls"));
    QCOMPARE(resp.toolCalls.size(), 1);
    QCOMPARE(resp.toolCalls.at(0).id, QStringLiteral("call_123"));
    QCOMPARE(resp.toolCalls.at(0).name, QStringLiteral("search"));
    QCOMPARE(resp.toolCalls.at(0).arguments, QStringLiteral("{\"q\":\"beethoven\"}"));
}

void TstLlmClient::streamingTruncatedWithoutDoneReturnsBadResponse()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    FakeLlmServer::Response r;
    r.status = 200;
    r.contentType = "text/event-stream";
    r.chunks.append(
        "data: " + QByteArray(R"({"choices":[{"delta":{"content":"Incomplete"}}]})") + "\n\n");
    server.enqueue(r);

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("key"),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    req.messages.append(userMessage(QStringLiteral("hi")));

    auto reply = client.stream(config, req);
    QSignalSpy finishSpy(reply.get(), &LlmReply::finished);

    QVERIFY(finishSpy.wait(2000));
    QVERIFY(reply->isFinished());
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kBadResponse);
}

void TstLlmClient::errorMappingAuth()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    QJsonObject errJson;
    errJson.insert(QStringLiteral("error"),
        QJsonObject { { QStringLiteral("message"), QStringLiteral("Invalid API key") } });
    server.enqueue(FakeLlmServer::json(errJson, 401));

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("bad-key"),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QCOMPARE(reply->httpStatus(), 401);
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kAuth);
    QVERIFY(reply->result().error().detail.contains(QStringLiteral("Invalid API key")));
}

void TstLlmClient::errorMappingRateLimitedWithRetryAfter()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    FakeLlmServer::Response r;
    r.status = 429;
    r.contentType = "application/json";
    r.headers.append(qMakePair(QByteArray("Retry-After"), QByteArray("3")));
    r.chunks.append(R"({"error":{"message":"Rate limit exceeded"}})");
    server.enqueue(r);

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("key"),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QCOMPARE(reply->httpStatus(), 429);
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kRateLimited);
    QCOMPARE(reply->retryAfterMs().value_or(-1), 3000);
}

void TstLlmClient::errorMappingHttpServerError()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    QJsonObject errJson;
    errJson.insert(QStringLiteral("error"),
        QJsonObject { { QStringLiteral("message"), QStringLiteral("Internal server error") } });
    server.enqueue(FakeLlmServer::json(errJson, 500));

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QString(),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QCOMPARE(reply->httpStatus(), 500);
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kHttp);
}

void TstLlmClient::errorMappingBadResponseNonJson()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    FakeLlmServer::Response r;
    r.status = 200;
    r.contentType = "text/html";
    r.chunks.append("<html><body>Not JSON</body></html>");
    server.enqueue(r);

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QString(),
        .model = QStringLiteral("gpt-4o"),
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kBadResponse);
}

void TstLlmClient::errorMappingTimeout()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    FakeLlmServer::Response r;
    r.hang = true;
    server.enqueue(r);

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QString(),
        .model = QStringLiteral("gpt-4o"),
        .timeoutMs = 150,
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(1500));
    QVERIFY(reply->isFinished());
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kTimeout);
}

void TstLlmClient::errorMappingNetworkConnectionRefused()
{
    quint16 closedPort = 0;
    {
        QTcpServer tempServer;
        tempServer.listen(QHostAddress::LocalHost, 0);
        closedPort = tempServer.serverPort();
        tempServer.close();
    }

    QNetworkAccessManager nam;
    LlmClient client(nam);

    ServiceConfig config {
        .baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(closedPort)),
        .apiKey = QString(),
        .model = QStringLiteral("gpt-4o"),
        .timeoutMs = 1000,
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);
    QVERIFY(spy.wait(2000));
    QVERIFY(reply->isFinished());
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kNetwork);
}

void TstLlmClient::abortInFlight()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    FakeLlmServer::Response r;
    r.hang = true;
    server.enqueue(r);

    ServiceConfig config {
        .baseUrl = server.baseUrl(),
        .apiKey = QString(),
        .model = QStringLiteral("gpt-4o"),
        .timeoutMs = 10000,
    };

    ChatRequest req;
    auto reply = client.complete(config, req);
    QSignalSpy spy(reply.get(), &LlmReply::finished);

    reply->abort();
    QCOMPARE(spy.count(), 1);
    QVERIFY(reply->isFinished());
    QVERIFY(!reply->result().ok());
    QCOMPARE(reply->result().error().code, linernotes::ai::errc::kAborted);

    // Call abort again - should be no-op and not emit signal again
    reply->abort();
    QCOMPARE(spy.count(), 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstLlmClient)

#include "tst_LlmClient.moc"
