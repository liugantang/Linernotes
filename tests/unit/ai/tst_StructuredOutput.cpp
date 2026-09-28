// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <ai/AiConfig.h>
#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/JsonSchema.h>
#include <ai/LlmCache.h>
#include <ai/LlmClient.h>
#include <ai/LlmDebugLog.h>
#include <ai/LlmReply.h>
#include <ai/LlmService.h>
#include <ai/PrivacyGuard.h>
#include <ai/SecretStore.h>
#include <ai/StructuredOutput.h>
#include <ai/UsageStore.h>
#include <common/ManualClock.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/Migrator.h>

using linernotes::ai::AiConfig;
using linernotes::ai::buildRepairRequest;
using linernotes::ai::buildStructuredRequest;
using linernotes::ai::CachePolicy;
using linernotes::ai::Capabilities;
using linernotes::ai::ChatRequest;
using linernotes::ai::ChatResponse;
using linernotes::ai::chooseStructuredMode;
using linernotes::ai::JsonSchema;
using linernotes::ai::LlmCache;
using linernotes::ai::LlmCall;
using linernotes::ai::LlmClient;
using linernotes::ai::LlmDebugLog;
using linernotes::ai::LlmReply;
using linernotes::ai::LlmService;
using linernotes::ai::LlmTask;
using linernotes::ai::MemorySecretStore;
using linernotes::ai::parseStructuredResponse;
using linernotes::ai::PrivacyGuard;
using linernotes::ai::Purpose;
using linernotes::ai::ResponseFormat;
using linernotes::ai::Role;
using linernotes::ai::ServiceConfig;
using linernotes::ai::ServiceProfile;
using linernotes::ai::StructuredMode;
using linernotes::ai::StructuredSpec;
using linernotes::ai::ToolCall;
using linernotes::ai::UsageStore;
using linernotes::core::Settings;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
namespace errc = linernotes::ai::errc;

namespace {

class HttpSseServer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(HttpSseServer)

public:
    explicit HttpSseServer(QList<QByteArray> chunks, QObject *parent = nullptr)
        : QObject(parent)
        , m_chunks(std::move(chunks))
    {
        connect(&m_server, &QTcpServer::newConnection, this, &HttpSseServer::onNewConnection);
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    ~HttpSseServer() override { m_server.close(); }

    QUrl url() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()));
    }

private:
    void onNewConnection()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: "
                              "close\r\n\r\n");
                for (const auto &chunk : m_chunks) {
                    socket->write(chunk);
                }
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    QTcpServer m_server;
    QList<QByteArray> m_chunks;
};

QJsonObject makeTestSchema()
{
    QJsonObject okProp;
    okProp.insert(QStringLiteral("type"), QStringLiteral("boolean"));

    QJsonObject props;
    props.insert(QStringLiteral("ok"), okProp);

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), props);
    schema.insert(QStringLiteral("required"), QJsonArray { QStringLiteral("ok") });
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

class TstStructuredOutput : public QObject {
    Q_OBJECT

private slots:
    void chooseStructuredModePriority_data();
    void chooseStructuredModePriority();

    void buildStructuredRequestTool();
    void buildStructuredRequestPromptWithExistingSystem();
    void buildStructuredRequestPromptWithoutSystem();
    void buildStructuredRequestJsonSchema();
    void buildStructuredRequestJsonObject();

    void parseStructuredResponseToolArguments();
    void parseStructuredResponseToolFallbackContent();
    void parseStructuredResponseMarkdownCodeBlock();
    void parseStructuredResponseSchemaMismatch();
    void parseStructuredResponseBadJson();

    void buildRepairRequestStandard();
    void buildRepairRequestToolMode();

    void structuredStreamWithReasoning();
    void streamingReasoningIgnoredInReply();
};

void TstStructuredOutput::chooseStructuredModePriority_data()
{
    QTest::addColumn<bool>("jsonSchema");
    QTest::addColumn<bool>("tools");
    QTest::addColumn<bool>("jsonObject");
    QTest::addColumn<StructuredMode>("expectedMode");

    QTest::newRow("json_schema_priority") << true << true << true << StructuredMode::JsonSchema;
    QTest::newRow("tool_priority") << false << true << true << StructuredMode::Tool;
    QTest::newRow("json_object_priority") << false << false << true << StructuredMode::JsonObject;
    QTest::newRow("prompt_fallback") << false << false << false << StructuredMode::Prompt;
}

void TstStructuredOutput::chooseStructuredModePriority()
{
    QFETCH(bool, jsonSchema);
    QFETCH(bool, tools);
    QFETCH(bool, jsonObject);
    QFETCH(StructuredMode, expectedMode);

    const Capabilities caps {
        .jsonSchema = jsonSchema,
        .tools = tools,
        .jsonObject = jsonObject,
    };
    QCOMPARE(chooseStructuredMode(caps), expectedMode);
}

void TstStructuredOutput::buildStructuredRequestTool()
{
    ChatRequest base;
    base.messages.append(makeMessage(Role::User, QStringLiteral("Do probe")));

    StructuredSpec spec {
        .name = QStringLiteral("report"),
        .description = QStringLiteral("Report result"),
        .schema = makeTestSchema(),
    };

    const auto req = buildStructuredRequest(base, spec, StructuredMode::Tool);
    QCOMPARE(req.tools.size(), 1);
    QCOMPARE(req.tools.at(0).name, QStringLiteral("report"));
    QCOMPARE(req.tools.at(0).description, QStringLiteral("Report result"));
    QCOMPARE(req.tools.at(0).parameters, makeTestSchema());
    QCOMPARE(req.forcedTool, QStringLiteral("report"));
    QCOMPARE(req.messages.size(), 1);
    QCOMPARE(req.messages.at(0).content, QStringLiteral("Do probe"));
}

void TstStructuredOutput::buildStructuredRequestPromptWithExistingSystem()
{
    ChatRequest base;
    base.messages.append(makeMessage(Role::System, QStringLiteral("Base system")));
    base.messages.append(makeMessage(Role::User, QStringLiteral("Hello")));

    StructuredSpec spec {
        .name = QStringLiteral("test"),
        .description = QStringLiteral("Test spec"),
        .schema = makeTestSchema(),
    };

    const auto req = buildStructuredRequest(base, spec, StructuredMode::Prompt);
    QCOMPARE(req.messages.size(), 2);
    QCOMPARE(req.messages.at(0).role, Role::System);
    QVERIFY(req.messages.at(0).content.startsWith(QStringLiteral("Base system\n\n")));
    QVERIFY(req.messages.at(0).content.contains(QStringLiteral("JSON Schema")));
    QVERIFY(req.messages.at(0).content.contains(QStringLiteral("\"additionalProperties\": false")));
    QCOMPARE(req.messages.at(1).role, Role::User);
}

void TstStructuredOutput::buildStructuredRequestPromptWithoutSystem()
{
    ChatRequest base;
    base.messages.append(makeMessage(Role::User, QStringLiteral("Hello")));

    StructuredSpec spec {
        .name = QStringLiteral("test"),
        .description = QStringLiteral("Test spec"),
        .schema = makeTestSchema(),
    };

    const auto req = buildStructuredRequest(base, spec, StructuredMode::Prompt);
    QCOMPARE(req.messages.size(), 2);
    QCOMPARE(req.messages.at(0).role, Role::System);
    QVERIFY(req.messages.at(0).content.contains(QStringLiteral("JSON Schema")));
    QCOMPARE(req.messages.at(1).role, Role::User);
    QCOMPARE(req.messages.at(1).content, QStringLiteral("Hello"));
}

void TstStructuredOutput::buildStructuredRequestJsonSchema()
{
    ChatRequest base;
    base.messages.append(makeMessage(Role::User, QStringLiteral("Hello")));

    StructuredSpec spec {
        .name = QStringLiteral("TestSchema"),
        .description = QStringLiteral("Test description"),
        .schema = makeTestSchema(),
    };

    const auto req = buildStructuredRequest(base, spec, StructuredMode::JsonSchema);
    QCOMPARE(req.responseFormat, ResponseFormat::JsonSchema);
    QCOMPARE(req.schemaName, QStringLiteral("TestSchema"));
    QCOMPARE(req.schema, makeTestSchema());
    QCOMPARE(req.messages.size(), 1);
    QCOMPARE(req.messages.at(0).content, QStringLiteral("Hello"));
}

void TstStructuredOutput::buildStructuredRequestJsonObject()
{
    ChatRequest base;
    base.messages.append(makeMessage(Role::User, QStringLiteral("Hello")));

    StructuredSpec spec {
        .name = QStringLiteral("test"),
        .description = QStringLiteral("Test spec"),
        .schema = makeTestSchema(),
    };

    const auto req = buildStructuredRequest(base, spec, StructuredMode::JsonObject);
    QCOMPARE(req.responseFormat, ResponseFormat::JsonObject);
    QCOMPARE(req.messages.size(), 2);
    QCOMPARE(req.messages.at(0).role, Role::System);
    QVERIFY(req.messages.at(0).content.contains(QStringLiteral("JSON Schema")));
}

void TstStructuredOutput::parseStructuredResponseToolArguments()
{
    const auto schemaRes = JsonSchema::compile(makeTestSchema());
    QVERIFY(schemaRes.ok());

    StructuredSpec spec {
        .name = QStringLiteral("report"),
        .description = QStringLiteral("Report tool"),
        .schema = makeTestSchema(),
    };

    ChatResponse resp;
    resp.toolCalls.append(ToolCall {
        .id = QStringLiteral("call_1"),
        .name = QStringLiteral("report"),
        .arguments = QStringLiteral("{\"ok\": true}"),
    });

    const auto result
        = parseStructuredResponse(resp, spec, schemaRes.value(), StructuredMode::Tool);
    QVERIFY(result.ok());
    QVERIFY(result.value().isObject());
    QCOMPARE(result.value().toObject().value(QStringLiteral("ok")).toBool(), true);
}

void TstStructuredOutput::parseStructuredResponseToolFallbackContent()
{
    const auto schemaRes = JsonSchema::compile(makeTestSchema());
    QVERIFY(schemaRes.ok());

    StructuredSpec spec {
        .name = QStringLiteral("report"),
        .description = QStringLiteral("Report tool"),
        .schema = makeTestSchema(),
    };

    ChatResponse resp;
    resp.content = QStringLiteral("{\"ok\": true}");

    const auto result
        = parseStructuredResponse(resp, spec, schemaRes.value(), StructuredMode::Tool);
    QVERIFY(result.ok());
    QVERIFY(result.value().isObject());
    QCOMPARE(result.value().toObject().value(QStringLiteral("ok")).toBool(), true);
}

void TstStructuredOutput::parseStructuredResponseMarkdownCodeBlock()
{
    const auto schemaRes = JsonSchema::compile(makeTestSchema());
    QVERIFY(schemaRes.ok());

    StructuredSpec spec {
        .name = QStringLiteral("test"),
        .description = QStringLiteral("Test spec"),
        .schema = makeTestSchema(),
    };

    ChatResponse resp;
    resp.content = QStringLiteral("Here is your output:\n```json\n{\"ok\": true}\n```\nEnjoy!");

    const auto result
        = parseStructuredResponse(resp, spec, schemaRes.value(), StructuredMode::Prompt);
    QVERIFY(result.ok());
    QVERIFY(result.value().isObject());
    QCOMPARE(result.value().toObject().value(QStringLiteral("ok")).toBool(), true);
}

void TstStructuredOutput::parseStructuredResponseSchemaMismatch()
{
    const auto schemaRes = JsonSchema::compile(makeTestSchema());
    QVERIFY(schemaRes.ok());

    StructuredSpec spec {
        .name = QStringLiteral("test"),
        .description = QStringLiteral("Test spec"),
        .schema = makeTestSchema(),
    };

    ChatResponse resp;
    resp.content = QStringLiteral("{\"ok\": 123}"); // wrong type

    const auto result
        = parseStructuredResponse(resp, spec, schemaRes.value(), StructuredMode::Prompt);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, QString(errc::kSchemaMismatch));
}

void TstStructuredOutput::parseStructuredResponseBadJson()
{
    const auto schemaRes = JsonSchema::compile(makeTestSchema());
    QVERIFY(schemaRes.ok());

    StructuredSpec spec {
        .name = QStringLiteral("test"),
        .description = QStringLiteral("Test spec"),
        .schema = makeTestSchema(),
    };

    ChatResponse resp;
    resp.content = QStringLiteral("Not JSON at all");

    const auto result
        = parseStructuredResponse(resp, spec, schemaRes.value(), StructuredMode::Prompt);
    QVERIFY(!result.ok());
    QCOMPARE(result.error().code, QString(errc::kBadJson));
}

void TstStructuredOutput::buildRepairRequestStandard()
{
    ChatRequest sent;
    sent.messages.append(makeMessage(Role::User, QStringLiteral("Original prompt")));
    sent.temperature = 0.5;

    ChatResponse badResp;
    badResp.content = QStringLiteral("Invalid raw output");

    linernotes::core::Error error {
        .code = QString(errc::kBadJson),
        .message = QStringLiteral("Failed to parse JSON"),
        .detail = QStringLiteral("Syntax error at line 1"),
    };

    const auto repairReq = buildRepairRequest(sent, badResp, error, StructuredMode::Prompt);
    QCOMPARE(repairReq.messages.size(), 3);
    QCOMPARE(repairReq.messages.at(0).role, Role::User);
    QCOMPARE(repairReq.messages.at(0).content, QStringLiteral("Original prompt"));

    QCOMPARE(repairReq.messages.at(1).role, Role::Assistant);
    QCOMPARE(repairReq.messages.at(1).content, QStringLiteral("Invalid raw output"));
    QVERIFY(repairReq.messages.at(1).toolCalls.isEmpty());

    QCOMPARE(repairReq.messages.at(2).role, Role::User);
    QVERIFY(repairReq.messages.at(2).content.contains(QStringLiteral("Syntax error at line 1")));

    QCOMPARE(repairReq.temperature, 0.5);
}

void TstStructuredOutput::buildRepairRequestToolMode()
{
    ChatRequest sent;
    sent.messages.append(makeMessage(Role::User, QStringLiteral("Call tool")));
    sent.forcedTool = QStringLiteral("report");

    ChatResponse badResp;
    badResp.toolCalls.append(ToolCall {
        .id = QStringLiteral("c1"),
        .name = QStringLiteral("report"),
        .arguments = QStringLiteral("{\"ok\": \"not boolean\"}"),
    });

    linernotes::core::Error error {
        .code = QString(errc::kSchemaMismatch),
        .message = QStringLiteral("Schema mismatch"),
        .detail = QStringLiteral("/ok : expected boolean"),
    };

    const auto repairReq = buildRepairRequest(sent, badResp, error, StructuredMode::Tool);
    QCOMPARE(repairReq.messages.size(), 3);
    QCOMPARE(repairReq.messages.at(1).role, Role::Assistant);
    QCOMPARE(repairReq.messages.at(1).content, QStringLiteral("{\"ok\": \"not boolean\"}"));
    QVERIFY(repairReq.messages.at(1).toolCalls.isEmpty());

    QCOMPARE(repairReq.messages.at(2).role, Role::User);
    QVERIFY(repairReq.messages.at(2).content.contains(QStringLiteral("/ok : expected boolean")));
    QCOMPARE(repairReq.forcedTool, QStringLiteral("report"));
}

void TstStructuredOutput::structuredStreamWithReasoning()
{
    const QList<QByteArray> chunks = {
        "data: {\"choices\":[{\"delta\":{\"reasoning\":\"Thinking step 1...\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Thinking step 2...\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"ok\\\":\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\" true}\"}}]}\n\n",
        QByteArray(R"(data: {"choices":[{"delta":{}}],)")
            + "\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":34}}\n\n",
        "data: [DONE]\n\n",
    };

    HttpSseServer server(chunks);

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_structured_stream.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000);
    Settings settings(tempDir.filePath(QStringLiteral("settings.ini")));
    AiConfig aiConfig(settings);

    ServiceProfile profile;
    profile.id = QStringLiteral("test-profile");
    profile.name = QStringLiteral("Test Server");
    profile.baseUrl = server.url();
    profile.defaultModel = QStringLiteral("test-model");
    profile.timeoutMs = 5000;
    profile.capabilities = Capabilities { .jsonSchema = true, .tools = false, .jsonObject = true };
    profile.maxConcurrent = 2;
    profile.requestsPerMinute = 0;
    aiConfig.saveService(profile);

    MemorySecretStore secrets;
    QNetworkAccessManager network;
    LlmClient client(network);
    LlmCache cache(db, clock);
    UsageStore usage(db);
    PrivacyGuard privacy(settings);
    LlmDebugLog debugLog(settings);
    LlmService service(aiConfig, secrets, client, cache, usage, privacy, debugLog, clock);

    StructuredSpec spec {
        .name = QStringLiteral("test_spec"),
        .description = QStringLiteral("Test Spec"),
        .schema = makeTestSchema(),
    };

    ChatRequest req;
    req.messages.append(makeMessage(Role::User, QStringLiteral("Run structured stream")));

    LlmCall call {
        .purpose = Purpose::Query,
        .request = std::move(req),
        .structured = spec,
        .stream = true,
        .cachePolicy = CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    auto task = service.start(std::move(call));
    QVERIFY(task != nullptr);

    QSignalSpy deltaSpy(task.get(), &LlmTask::delta);
    QSignalSpy finishSpy(task.get(), &LlmTask::finished);

    QVERIFY(finishSpy.wait(5000));
    QVERIFY(task->isFinished());
    QVERIFY(task->result().ok());

    const auto &res = task->result().value();
    QVERIFY(res.structured.has_value());
    if (!res.structured.has_value()) {
        return;
    }
    QVERIFY(res.structured->isObject());
    QCOMPARE(res.structured->toObject().value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(res.totalUsage.promptTokens, 12);
    QCOMPARE(res.totalUsage.completionTokens, 34);
    QCOMPARE(deltaSpy.count(), 0);
}

void TstStructuredOutput::streamingReasoningIgnoredInReply()
{
    const QList<QByteArray> chunks = {
        "data: {\"choices\":[{\"delta\":{\"reasoning\":\"Thinking step 1...\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"Thinking step 2...\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"ok\\\":\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\" true}\"}}]}\n\n",
        QByteArray(R"(data: {"choices":[{"delta":{}}],)")
            + "\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":34}}\n\n",
        "data: [DONE]\n\n",
    };

    HttpSseServer server(chunks);

    QNetworkAccessManager network;
    LlmClient client(network);

    ServiceConfig config {
        .baseUrl = server.url(),
        .apiKey = QString(),
        .model = QStringLiteral("test-model"),
        .timeoutMs = 5000,
    };

    ChatRequest req;
    req.messages.append(makeMessage(Role::User, QStringLiteral("hi")));

    auto reply = client.stream(config, req);
    QVERIFY(reply != nullptr);

    QSignalSpy deltaSpy(reply.get(), &LlmReply::delta);
    QSignalSpy finishSpy(reply.get(), &LlmReply::finished);

    QVERIFY(finishSpy.wait(5000));
    QVERIFY(reply->isFinished());
    QVERIFY(reply->result().ok());

    const auto &resp = reply->result().value();
    QCOMPARE(resp.content, QStringLiteral("{\"ok\": true}"));
    QVERIFY(!resp.content.contains(QStringLiteral("Thinking")));
    QCOMPARE(resp.usage.promptTokens, 12);
    QCOMPARE(resp.usage.completionTokens, 34);

    QCOMPARE(deltaSpy.count(), 2);
    QCOMPARE(deltaSpy.at(0).at(0).toString(), QStringLiteral("{\"ok\":"));
    QCOMPARE(deltaSpy.at(1).at(0).toString(), QStringLiteral(" true}"));
}

} // namespace

QTEST_GUILESS_MAIN(TstStructuredOutput)

#include "tst_StructuredOutput.moc"
