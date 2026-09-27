// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QObject>
#include <QStringList>
#include <QTest>

#include <ai/Errors.h>
#include <ai/JsonSchema.h>

using linernotes::ai::extractJson;
using linernotes::ai::JsonSchema;
namespace errc = linernotes::ai::errc;

namespace {

class TstJsonSchema : public QObject {
    Q_OBJECT

private slots:
    void validInstancePasses();
    void schemaMismatchErrors();
    void invalidSchema();
    void extractJsonFromText_data();
    void extractJsonFromText();
};

void TstJsonSchema::validInstancePasses()
{
    const QJsonObject schema = QJsonDocument::fromJson(R"({
        "$schema": "http://json-schema.org/draft-07/schema#",
        "type": "object",
        "properties": {
            "title": {"type": "string"},
            "artist": {"type": "string"},
            "year": {"type": "integer"}
        },
        "required": ["title", "artist"]
    })")
                                   .object();

    const auto compiled = JsonSchema::compile(schema);
    QVERIFY(compiled.ok());

    const QJsonObject validInstance = QJsonDocument::fromJson(R"({
        "title": "Bohemian Rhapsody",
        "artist": "Queen",
        "year": 1975
    })")
                                          .object();

    const auto result = compiled.value().validate(validInstance);
    QVERIFY(result.ok());
}

void TstJsonSchema::schemaMismatchErrors()
{
    // 1. Missing required field
    const QJsonObject requiredSchema = QJsonDocument::fromJson(R"({
        "type": "object",
        "properties": {
            "name": {"type": "string"}
        },
        "required": ["name"]
    })")
                                           .object();

    const auto compiledRequired = JsonSchema::compile(requiredSchema);
    QVERIFY(compiledRequired.ok());

    const auto missingFieldRes = compiledRequired.value().validate(QJsonObject());
    QVERIFY(!missingFieldRes.ok());
    QCOMPARE(missingFieldRes.error().code, QString(errc::kSchemaMismatch));
    QVERIFY(missingFieldRes.error().detail.contains(QStringLiteral("/")));

    // 2. Wrong type at path
    const QJsonObject nestedSchema = QJsonDocument::fromJson(R"({
        "type": "object",
        "properties": {
            "items": {
                "type": "array",
                "items": {
                    "type": "object",
                    "properties": {
                        "name": {"type": "string"}
                    },
                    "required": ["name"]
                }
            }
        }
    })")
                                         .object();

    const auto compiledNested = JsonSchema::compile(nestedSchema);
    QVERIFY(compiledNested.ok());

    const QJsonObject wrongTypeInstance = QJsonDocument::fromJson(R"({
        "items": [
            {"name": "first"},
            {"name": 123}
        ]
    })")
                                              .object();

    const auto wrongTypeRes = compiledNested.value().validate(wrongTypeInstance);
    QVERIFY(!wrongTypeRes.ok());
    QCOMPARE(wrongTypeRes.error().code, QString(errc::kSchemaMismatch));
    QVERIFY(wrongTypeRes.error().detail.contains(QStringLiteral("/items/1/name")));

    // 3. Enum out of range
    const QJsonObject enumSchema = QJsonDocument::fromJson(R"({
        "type": "object",
        "properties": {
            "status": {"enum": ["pending", "done"]}
        }
    })")
                                       .object();

    const auto compiledEnum = JsonSchema::compile(enumSchema);
    QVERIFY(compiledEnum.ok());

    const QJsonObject invalidEnumInstance = QJsonDocument::fromJson(R"({
        "status": "unknown"
    })")
                                                .object();

    const auto enumRes = compiledEnum.value().validate(invalidEnumInstance);
    QVERIFY(!enumRes.ok());
    QCOMPARE(enumRes.error().code, QString(errc::kSchemaMismatch));
    QVERIFY(enumRes.error().detail.contains(QStringLiteral("/status")));

    // 4. Additional properties forbidden
    const QJsonObject strictSchema = QJsonDocument::fromJson(R"({
        "type": "object",
        "properties": {
            "name": {"type": "string"}
        },
        "additionalProperties": false
    })")
                                         .object();

    const auto compiledStrict = JsonSchema::compile(strictSchema);
    QVERIFY(compiledStrict.ok());

    const QJsonObject extraPropInstance = QJsonDocument::fromJson(R"({
        "name": "test",
        "extra": 42
    })")
                                              .object();

    const auto strictRes = compiledStrict.value().validate(extraPropInstance);
    QVERIFY(!strictRes.ok());
    QCOMPARE(strictRes.error().code, QString(errc::kSchemaMismatch));

    // 5. Two errors simultaneously produce two lines in detail
    const QJsonObject twoErrorsInstance = QJsonDocument::fromJson(R"({
        "items": [
            {"name": 10},
            {"name": 20}
        ]
    })")
                                              .object();

    const auto twoErrorsRes = compiledNested.value().validate(twoErrorsInstance);
    QVERIFY(!twoErrorsRes.ok());
    QCOMPARE(twoErrorsRes.error().code, QString(errc::kSchemaMismatch));
    const QStringList lines = twoErrorsRes.error().detail.split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 2);
    QVERIFY(twoErrorsRes.error().detail.contains(QStringLiteral("/items/0/name")));
    QVERIFY(twoErrorsRes.error().detail.contains(QStringLiteral("/items/1/name")));
}

void TstJsonSchema::invalidSchema()
{
    const QJsonObject badSchema = QJsonDocument::fromJson(R"({
        "type": 123
    })")
                                      .object();

    const auto compiled = JsonSchema::compile(badSchema);
    QVERIFY(!compiled.ok());
    QCOMPARE(compiled.error().code, QString(errc::kSchemaInvalid));
    QVERIFY(!compiled.error().detail.isEmpty());
}

void TstJsonSchema::extractJsonFromText_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("expectOk");
    QTest::addColumn<QJsonValue>("expectedValue");

    QTest::newRow("pure_object")
        << QStringLiteral(R"({"name": "pure", "count": 42})") << true
        << QJsonValue(QJsonDocument::fromJson(R"({"name": "pure", "count": 42})").object());

    QTest::newRow("pure_array") << QStringLiteral(R"(["a", "b", "c"])") << true
                                << QJsonValue(
                                       QJsonDocument::fromJson(R"(["a", "b", "c"])").array());

    QTest::newRow("code_block_json")
        << QStringLiteral("Here is the JSON:\n```json\n{\"key\": \"val\"}\n```\nHope it helps!")
        << true << QJsonValue(QJsonDocument::fromJson(R"({"key": "val"})").object());

    QTest::newRow("code_block_no_lang")
        << QStringLiteral("```\n[1, 2, 3]\n```") << true
        << QJsonValue(QJsonDocument::fromJson(R"([1, 2, 3])").array());

    QTest::newRow("surrounding_text_object")
        << QStringLiteral("The result is: {\"status\": \"ok\", \"code\": 200}. Thank you.") << true
        << QJsonValue(QJsonDocument::fromJson(R"({"status": "ok", "code": 200})").object());

    QTest::newRow("surrounding_text_array")
        << QStringLiteral("Tags found: [\"rock\", \"pop\", \"jazz\"] in library.") << true
        << QJsonValue(QJsonDocument::fromJson(R"(["rock", "pop", "jazz"])").array());

    QTest::newRow("no_json") << QStringLiteral("Sorry, I could not process your request.") << false
                             << QJsonValue();

    QTest::newRow("empty_text") << QStringLiteral("") << false << QJsonValue();
}

void TstJsonSchema::extractJsonFromText()
{
    QFETCH(QString, input);
    QFETCH(bool, expectOk);
    QFETCH(QJsonValue, expectedValue);

    const auto result = extractJson(input);
    if (expectOk) {
        QVERIFY(result.ok());
        QCOMPARE(result.value(), expectedValue);
    } else {
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, QString(errc::kBadJson));
        QVERIFY(!result.error().detail.isEmpty());
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstJsonSchema)

#include "tst_JsonSchema.moc"
