// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <ai/CapabilityProbe.h>
#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/LlmClient.h>
#include <common/FakeLlmServer.h>

using linernotes::ai::Capabilities;
using linernotes::ai::CapabilityProbe;
using linernotes::ai::LlmClient;
using linernotes::ai::ServiceConfig;
using linernotes::test::FakeLlmServer;
namespace errc = linernotes::ai::errc;

namespace {

FakeLlmServer::Response makeJsonResponse(const QString &content, int status = 200)
{
    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msgObj.insert(QStringLiteral("content"), content);

    QJsonObject choice0;
    choice0.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));
    choice0.insert(QStringLiteral("message"), msgObj);

    QJsonObject respObj;
    respObj.insert(QStringLiteral("choices"), QJsonArray { choice0 });
    return FakeLlmServer::json(respObj, status);
}

FakeLlmServer::Response makeToolCallResponse(const QString &fnName, const QString &arguments)
{
    QJsonObject fnObj;
    fnObj.insert(QStringLiteral("name"), fnName);
    fnObj.insert(QStringLiteral("arguments"), arguments);

    QJsonObject tcObj;
    tcObj.insert(QStringLiteral("id"), QStringLiteral("call_1"));
    tcObj.insert(QStringLiteral("type"), QStringLiteral("function"));
    tcObj.insert(QStringLiteral("function"), fnObj);

    QJsonObject msgObj;
    msgObj.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msgObj.insert(QStringLiteral("tool_calls"), QJsonArray { tcObj });

    QJsonObject choice0;
    choice0.insert(QStringLiteral("finish_reason"), QStringLiteral("tool_calls"));
    choice0.insert(QStringLiteral("message"), msgObj);

    QJsonObject respObj;
    respObj.insert(QStringLiteral("choices"), QJsonArray { choice0 });
    return FakeLlmServer::json(respObj, 200);
}

class TstCapabilityProbe : public QObject {
    Q_OBJECT

private slots:
    void allCapabilitiesSupported();
    void partialCapabilitiesSupported();
    void authErrorStopsImmediately();
};

void TstCapabilityProbe::allCapabilitiesSupported()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    // 1. json_schema probe response
    server.enqueue(makeJsonResponse(QStringLiteral("{\"ok\": true}")));
    // 2. tools probe response
    server.enqueue(
        makeToolCallResponse(QStringLiteral("report"), QStringLiteral("{\"ok\": true}")));
    // 3. json_object probe response
    server.enqueue(makeJsonResponse(QStringLiteral("{\"ok\": true}")));

    ServiceConfig service {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("test-key"),
        .model = QStringLiteral("gpt-4o"),
        .timeoutMs = 5000,
    };

    CapabilityProbe probe(client, service);
    QSignalSpy spy(&probe, &CapabilityProbe::finished);
    probe.start();

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(probe.result().ok());

    const Capabilities expected {
        .jsonSchema = true,
        .tools = true,
        .jsonObject = true,
    };
    QCOMPARE(probe.result().value(), expected);

    const auto reqs = server.requests();
    QCOMPARE(reqs.size(), 3);

    // Request 0: json_schema
    const QJsonObject req0 = QJsonDocument::fromJson(reqs.at(0).body).object();
    QVERIFY(req0.contains(QStringLiteral("response_format")));
    QCOMPARE(req0.value(QStringLiteral("response_format"))
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
        QStringLiteral("json_schema"));
    QCOMPARE(req0.value(QStringLiteral("temperature")).toDouble(), 0.0);
    QCOMPARE(req0.value(QStringLiteral("max_tokens")).toInt(), 50);

    // Request 1: tools
    const QJsonObject req1 = QJsonDocument::fromJson(reqs.at(1).body).object();
    QVERIFY(req1.contains(QStringLiteral("tools")));
    QVERIFY(req1.contains(QStringLiteral("tool_choice")));
    QCOMPARE(req1.value(QStringLiteral("tool_choice"))
                 .toObject()
                 .value(QStringLiteral("function"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
        QStringLiteral("report"));
    QCOMPARE(req1.value(QStringLiteral("temperature")).toDouble(), 0.0);
    QCOMPARE(req1.value(QStringLiteral("max_tokens")).toInt(), 50);

    // Request 2: json_object
    const QJsonObject req2 = QJsonDocument::fromJson(reqs.at(2).body).object();
    QVERIFY(req2.contains(QStringLiteral("response_format")));
    QCOMPARE(req2.value(QStringLiteral("response_format"))
                 .toObject()
                 .value(QStringLiteral("type"))
                 .toString(),
        QStringLiteral("json_object"));
    QCOMPARE(req2.value(QStringLiteral("temperature")).toDouble(), 0.0);
    QCOMPARE(req2.value(QStringLiteral("max_tokens")).toInt(), 50);
}

void TstCapabilityProbe::partialCapabilitiesSupported()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    // 1. json_schema returns 400
    QJsonObject err400;
    err400.insert(QStringLiteral("error"),
        QJsonObject {
            { QStringLiteral("message"), QStringLiteral("Unsupported response_format") } });
    server.enqueue(FakeLlmServer::json(err400, 400));

    // 2. tools normal
    server.enqueue(
        makeToolCallResponse(QStringLiteral("report"), QStringLiteral("{\"ok\": true}")));

    // 3. json_object returns content that is not JSON
    server.enqueue(makeJsonResponse(QStringLiteral("not valid json at all")));

    ServiceConfig service {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("test-key"),
        .model = QStringLiteral("local-model"),
        .timeoutMs = 5000,
    };

    CapabilityProbe probe(client, service);
    QSignalSpy spy(&probe, &CapabilityProbe::finished);
    probe.start();

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(probe.result().ok());

    const Capabilities expected {
        .jsonSchema = false,
        .tools = true,
        .jsonObject = false,
    };
    QCOMPARE(probe.result().value(), expected);

    const auto reqs = server.requests();
    QCOMPARE(reqs.size(), 3);
}

void TstCapabilityProbe::authErrorStopsImmediately()
{
    FakeLlmServer server;
    QNetworkAccessManager nam;
    LlmClient client(nam);

    // 1. json_schema returns 401
    QJsonObject err401;
    err401.insert(QStringLiteral("error"),
        QJsonObject { { QStringLiteral("message"), QStringLiteral("Unauthorized API key") } });
    server.enqueue(FakeLlmServer::json(err401, 401));

    ServiceConfig service {
        .baseUrl = server.baseUrl(),
        .apiKey = QStringLiteral("bad-key"),
        .model = QStringLiteral("gpt-4o"),
        .timeoutMs = 5000,
    };

    CapabilityProbe probe(client, service);
    QSignalSpy spy(&probe, &CapabilityProbe::finished);
    probe.start();

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(!probe.result().ok());
    QCOMPARE(probe.result().error().code, QString(errc::kAuth));

    // Only 1 request was sent before stopping
    const auto reqs = server.requests();
    QCOMPARE(reqs.size(), 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstCapabilityProbe)

#include "tst_CapabilityProbe.moc"
