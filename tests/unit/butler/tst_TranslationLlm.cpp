// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <butler/TranslationLlm.h>
#include <butler/TranslationSource.h>

namespace {

using linernotes::butler::needsTranslation;
using linernotes::butler::parseTranslationResult;
using linernotes::butler::translationPromptVars;
using linernotes::butler::translationSchema;

class TstTranslationLlm : public QObject {
    Q_OBJECT

private slots:
    void needsTranslationTests();
    void parseNormalAndEmptyTranslation();
    void skipOutOfBoundsAndDuplicateId();
    void parseNonObjectOrMissingItemsFails();
    void promptVarsAndSchema();
};

void TstTranslationLlm::needsTranslationTests()
{
    QCOMPARE(needsTranslation(QStringLiteral("あぁ光塚歌劇団")), true);
    QCOMPARE(needsTranslation(QStringLiteral("Snow halation")), true);
    QCOMPARE(needsTranslation(QStringLiteral("炎")), false);
    QCOMPARE(needsTranslation(QStringLiteral("宝物")), false);
    QCOMPARE(needsTranslation(QStringLiteral("M16")), false); // 只有 1 个拉丁字母：编号，不翻译
    QCOMPARE(needsTranslation(QStringLiteral("2016")), false);
    QCOMPARE(needsTranslation(QStringLiteral("사랑")), true);
}

void TstTranslationLlm::parseNormalAndEmptyTranslation()
{
    const QStringList texts = {
        QStringLiteral("Snow halation (Extended Mix)"),
        QStringLiteral("EVA-01 (E-3)"),
        QStringLiteral("言の葉 青葉"),
    };

    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    obj1.insert(QStringLiteral("translation"), QStringLiteral("雪之光晕（加长混音）"));

    QJsonObject obj2;
    obj2.insert(QStringLiteral("id"), 2);
    obj2.insert(QStringLiteral("translation"), QStringLiteral(""));

    QJsonObject obj3;
    obj3.insert(QStringLiteral("id"), 3);
    obj3.insert(QStringLiteral("translation"), QStringLiteral("言之叶 青叶"));

    QJsonArray itemsArr;
    itemsArr.append(obj1);
    itemsArr.append(obj2);
    itemsArr.append(obj3);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseTranslationResult(root, texts);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 3);
    QVERIFY(map.contains(QStringLiteral("Snow halation (Extended Mix)")));
    QVERIFY(map.contains(QStringLiteral("EVA-01 (E-3)")));
    QVERIFY(map.contains(QStringLiteral("言の葉 青葉")));

    QCOMPARE(map.value(QStringLiteral("Snow halation (Extended Mix)")),
        QStringLiteral("雪之光晕（加长混音）"));
    QCOMPARE(map.value(QStringLiteral("EVA-01 (E-3)")), QStringLiteral(""));
    QCOMPARE(map.value(QStringLiteral("言の葉 青葉")), QStringLiteral("言之叶 青叶"));
}

void TstTranslationLlm::skipOutOfBoundsAndDuplicateId()
{
    const QStringList texts = {
        QStringLiteral("Snow halation"),
    };

    // id: 99 (out of bounds) -> skip
    QJsonObject objOob;
    objOob.insert(QStringLiteral("id"), 99);
    objOob.insert(QStringLiteral("translation"), QStringLiteral("Wrong"));

    // id: 1 (valid)
    QJsonObject objValid;
    objValid.insert(QStringLiteral("id"), 1);
    objValid.insert(QStringLiteral("translation"), QStringLiteral("雪之光晕"));

    // id: 1 (duplicate) -> skip
    QJsonObject objDup;
    objDup.insert(QStringLiteral("id"), 1);
    objDup.insert(QStringLiteral("translation"), QStringLiteral("Duplicate"));

    QJsonArray itemsArr;
    itemsArr.append(objOob);
    itemsArr.append(objValid);
    itemsArr.append(objDup);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseTranslationResult(root, texts);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 1);
    QVERIFY(map.contains(QStringLiteral("Snow halation")));
    QCOMPARE(map.value(QStringLiteral("Snow halation")), QStringLiteral("雪之光晕"));
}

void TstTranslationLlm::parseNonObjectOrMissingItemsFails()
{
    const QStringList texts = {
        QStringLiteral("Snow halation"),
    };

    // Non-object
    const auto res1 = parseTranslationResult(QJsonValue(QStringLiteral("not an object")), texts);
    QVERIFY(!res1.ok());

    // Missing "items"
    QJsonObject emptyObj;
    const auto res2 = parseTranslationResult(emptyObj, texts);
    QVERIFY(!res2.ok());

    // "items" is not an array
    QJsonObject invalidItemsObj;
    invalidItemsObj.insert(QStringLiteral("items"), QStringLiteral("not array"));
    const auto res3 = parseTranslationResult(invalidItemsObj, texts);
    QVERIFY(!res3.ok());
}

void TstTranslationLlm::promptVarsAndSchema()
{
    const QStringList texts = {
        QStringLiteral("あぁ光塚歌劇団"),
        QStringLiteral("Snow halation"),
    };

    const auto vars = translationPromptVars(texts);
    QVERIFY(vars.contains(QStringLiteral("items")));
    const QString itemsStr = vars.value(QStringLiteral("items"));
    QVERIFY(itemsStr.contains(QStringLiteral("ID 1: あぁ光塚歌劇団")));
    QVERIFY(itemsStr.contains(QStringLiteral("ID 2: Snow halation")));

    const auto emptyVars = translationPromptVars({ });
    QCOMPARE(emptyVars.value(QStringLiteral("items")), QStringLiteral("(none)"));

    const auto schema = translationSchema();
    QVERIFY(!schema.isEmpty());
    QCOMPARE(schema.value(QStringLiteral("title")).toString(), QStringLiteral("TranslateTitles"));
}

} // namespace

QTEST_GUILESS_MAIN(TstTranslationLlm)

#include "tst_TranslationLlm.moc"
