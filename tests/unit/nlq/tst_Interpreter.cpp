// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QObject>
#include <QSet>
#include <QTest>

#include <ai/JsonSchema.h>
#include <ai/PromptLibrary.h>
#include <library/LibraryEnums.h>
#include <library/SmartRule.h>
#include <nlq/Interpreter.h>
#include <nlq/NlqQuery.h>

namespace {

using namespace linernotes;
using namespace linernotes::nlq;

class TstInterpreter : public QObject {
    Q_OBJECT

private slots:
    void promptVarsWithoutPrevious();
    void promptVarsWithPrevious();
    void parseInterpretationValid();
    void parseInterpretationErrors();
    void promptTemplateAndSchemaConsistency();
};

void TstInterpreter::promptVarsWithoutPrevious()
{
    const QString question = QStringLiteral("我最常听的歌");
    const QString summary = QStringLiteral("曲库：100 首，10 张专辑");
    const auto vars = nlqPromptVars(question, summary, std::nullopt);

    QCOMPARE(vars.value(QStringLiteral("previous_query")), QStringLiteral("(none)"));
    QCOMPARE(vars.value(QStringLiteral("question")), question);
    QCOMPARE(vars.value(QStringLiteral("library_summary")), summary);
}

void TstInterpreter::promptVarsWithPrevious()
{
    Query q;
    q.entity = Entity::Album;
    q.rule.match = library::SmartMatch::All;
    q.rule.conditions = {
        library::SmartCondition {
            .field = library::SmartField::Language,
            .op = library::SmartOp::Is,
            .value = QStringLiteral("ja"),
            .value2 = { },
        },
    };
    q.sortKey = SortKey::PlayCount;
    q.sortOrder = Qt::DescendingOrder;
    q.limit = 20;

    const QString question = QStringLiteral("只要现场版");
    const QString summary = QStringLiteral("曲库：100 首");
    const auto vars = nlqPromptVars(question, summary, q);

    const QString prevStr = vars.value(QStringLiteral("previous_query"));
    QVERIFY(prevStr != QStringLiteral("(none)"));

    QJsonParseError parseErr { };
    const QJsonDocument doc = QJsonDocument::fromJson(prevStr.toUtf8(), &parseErr);
    QCOMPARE(parseErr.error, QJsonParseError::NoError);
    QVERIFY(doc.isObject());

    const auto parsedQueryRes = Query::fromJson(doc.object());
    QVERIFY(parsedQueryRes.ok());
    QCOMPARE(parsedQueryRes.value(), q);
    QCOMPARE(vars.value(QStringLiteral("question")), question);
    QCOMPARE(vars.value(QStringLiteral("library_summary")), summary);
}

void TstInterpreter::parseInterpretationValid()
{
    QJsonObject validObj;
    QJsonObject queryObj;
    queryObj.insert(QStringLiteral("entity"), QStringLiteral("track"));
    queryObj.insert(QStringLiteral("match"), QStringLiteral("all"));

    QJsonArray condArray;
    QJsonObject condObj;
    condObj.insert(QStringLiteral("field"), QStringLiteral("artist"));
    condObj.insert(QStringLiteral("op"), QStringLiteral("contains"));
    condObj.insert(QStringLiteral("value"), QStringLiteral("周杰伦"));
    condArray.append(condObj);

    queryObj.insert(QStringLiteral("conditions"), condArray);
    queryObj.insert(QStringLiteral("sort"), QStringLiteral("year"));
    queryObj.insert(QStringLiteral("order"), QStringLiteral("asc"));
    queryObj.insert(QStringLiteral("limit"), 50);

    validObj.insert(QStringLiteral("query"), queryObj);
    validObj.insert(QStringLiteral("explanation"), QStringLiteral("周杰伦的全部歌曲"));

    const auto res = parseInterpretation(validObj);
    QVERIFY(res.ok());
    const auto &interp = res.value();
    QCOMPARE(interp.explanation, QStringLiteral("周杰伦的全部歌曲"));
    QCOMPARE(interp.query.entity, Entity::Track);
    QCOMPARE(interp.query.sortKey, SortKey::Year);
    QCOMPARE(interp.query.sortOrder, Qt::AscendingOrder);
    QCOMPARE(interp.query.limit, 50);
    QCOMPARE(interp.query.rule.conditions.size(), 1);
    QCOMPARE(interp.query.rule.conditions.at(0).field, library::SmartField::Artist);
    QCOMPARE(interp.query.rule.conditions.at(0).op, library::SmartOp::Contains);
    QCOMPARE(interp.query.rule.conditions.at(0).value.toString(), QStringLiteral("周杰伦"));
}

void TstInterpreter::parseInterpretationErrors()
{
    // Non-object
    {
        const auto res = parseInterpretation(QJsonValue(QStringLiteral("not an object")));
        QVERIFY(!res.ok());
    }

    // Missing query
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("explanation"), QStringLiteral("没有 query"));
        const auto res = parseInterpretation(obj);
        QVERIFY(!res.ok());
    }

    // Missing explanation
    {
        QJsonObject obj;
        QJsonObject queryObj;
        queryObj.insert(QStringLiteral("entity"), QStringLiteral("track"));
        obj.insert(QStringLiteral("query"), queryObj);
        const auto res = parseInterpretation(obj);
        QVERIFY(!res.ok());
    }

    // Unknown field name in query
    {
        QJsonObject obj;
        QJsonObject queryObj;
        queryObj.insert(QStringLiteral("entity"), QStringLiteral("track"));
        QJsonArray condArray;
        QJsonObject badCond;
        badCond.insert(QStringLiteral("field"), QStringLiteral("unknown_field_xyz"));
        badCond.insert(QStringLiteral("op"), QStringLiteral("contains"));
        badCond.insert(QStringLiteral("value"), QStringLiteral("test"));
        condArray.append(badCond);
        queryObj.insert(QStringLiteral("conditions"), condArray);

        obj.insert(QStringLiteral("query"), queryObj);
        obj.insert(QStringLiteral("explanation"), QStringLiteral("未知字段"));

        const auto res = parseInterpretation(obj);
        QVERIFY(!res.ok());
    }
}

void TstInterpreter::promptTemplateAndSchemaConsistency()
{
    // 1. PromptLibrary can render nlq/query with all variables
    ai::PromptLibrary promptLib(QStringList { QStringLiteral(":/prompts") });
    const auto loadRes = promptLib.load(QStringLiteral("nlq/query"));
    QVERIFY(loadRes.ok());
    const auto &tmpl = loadRes.value();
    QVERIFY(tmpl.version >= 1);
    QVERIFY(!tmpl.system.isEmpty());
    QVERIFY(!tmpl.user.isEmpty());

    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("library_summary"), QStringLiteral("今天：2026-10-01"));
    vars.insert(QStringLiteral("previous_query"), QStringLiteral("(none)"));
    vars.insert(QStringLiteral("question"), QStringLiteral("周杰伦所有歌"));
    const auto renderRes = promptLib.render(QStringLiteral("nlq/query"), vars);
    QVERIFY(renderRes.ok());
    const auto &rendered = renderRes.value();
    QVERIFY(rendered.user.contains(QStringLiteral("周杰伦所有歌")));
    QVERIFY(rendered.user.contains(QStringLiteral("今天：2026-10-01")));

    // 2. Schema can be compiled by ai::JsonSchema
    const QJsonObject schemaObj = nlqQuerySchema();
    QVERIFY(!schemaObj.isEmpty());
    const auto schemaRes = ai::JsonSchema::compile(schemaObj);
    QVERIFY(schemaRes.ok());

    // 3. Schema conditions.field enum matches SmartField enum values 1-to-1
    const QJsonObject properties = schemaObj.value(QStringLiteral("properties")).toObject();
    const QJsonObject queryProp = properties.value(QStringLiteral("query")).toObject();
    const QJsonObject queryProperties = queryProp.value(QStringLiteral("properties")).toObject();
    const QJsonObject conditionsProp
        = queryProperties.value(QStringLiteral("conditions")).toObject();
    const QJsonObject itemsProp = conditionsProp.value(QStringLiteral("items")).toObject();
    const QJsonObject itemProperties = itemsProp.value(QStringLiteral("properties")).toObject();
    const QJsonObject fieldProp = itemProperties.value(QStringLiteral("field")).toObject();
    const QJsonArray fieldEnums = fieldProp.value(QStringLiteral("enum")).toArray();

    QSet<QString> schemaFields;
    for (const auto &v : fieldEnums) {
        schemaFields.insert(v.toString());
    }

    const QMetaEnum meta = QMetaEnum::fromType<library::SmartField>();
    QSet<QString> codeFields;
    for (int i = 0; i < meta.keyCount(); ++i) {
        const auto sf = static_cast<library::SmartField>(meta.value(i));
        library::SmartRule r;
        r.conditions = {
            library::SmartCondition {
                .field = sf,
                .op = library::SmartOp::Contains,
                .value = QStringLiteral("v"),
                .value2 = { },
            },
        };
        const QJsonObject rObj = r.toJsonObject();
        const QJsonArray conds = rObj.value(QStringLiteral("conditions")).toArray();
        QVERIFY(!conds.isEmpty());
        const QString fieldName = conds.at(0).toObject().value(QStringLiteral("field")).toString();
        QVERIFY(!fieldName.isEmpty());
        codeFields.insert(fieldName);
    }

    QCOMPARE(schemaFields, codeFields);
}

} // namespace

QTEST_MAIN(TstInterpreter)
#include "tst_Interpreter.moc"
