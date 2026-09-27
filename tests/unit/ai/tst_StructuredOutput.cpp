// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QTest>

#include <ai/ChatTypes.h>
#include <ai/Errors.h>
#include <ai/JsonSchema.h>
#include <ai/StructuredOutput.h>

using linernotes::ai::buildRepairRequest;
using linernotes::ai::buildStructuredRequest;
using linernotes::ai::Capabilities;
using linernotes::ai::ChatRequest;
using linernotes::ai::ChatResponse;
using linernotes::ai::chooseStructuredMode;
using linernotes::ai::JsonSchema;
using linernotes::ai::parseStructuredResponse;
using linernotes::ai::ResponseFormat;
using linernotes::ai::Role;
using linernotes::ai::StructuredMode;
using linernotes::ai::StructuredSpec;
using linernotes::ai::ToolCall;
namespace errc = linernotes::ai::errc;

namespace {

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

} // namespace

QTEST_GUILESS_MAIN(TstStructuredOutput)

#include "tst_StructuredOutput.moc"
