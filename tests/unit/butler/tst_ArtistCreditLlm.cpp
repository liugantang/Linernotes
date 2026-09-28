// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include <butler/ArtistCredit.h>
#include <butler/ArtistCreditLlm.h>
#include <common/TestSupport.h>

namespace {

using linernotes::butler::ArtistCredit;
using linernotes::butler::artistCreditFromJson;
using linernotes::butler::CreditPerformer;
using linernotes::butler::normalizedValue;
using linernotes::butler::parseArtistCreditResult;
using linernotes::butler::toJson;

class TstArtistCreditLlm : public QObject {
    Q_OBJECT

private slots:
    void parseValidAndSkipInvalid();
    void omittedValuesGetAsIsResult();
    void emptyItemsArrayGivesAllAsIs();
    void optionalAkaAndRolesParsedAsEmpty();
    void parseMissingItemsFails();
    void roundTripAndNormalizedValue();
};

void TstArtistCreditLlm::parseValidAndSkipInvalid()
{
    const QStringList values = {
        QStringLiteral("高垣彩陽（as 雪音クリス）"),
        QStringLiteral("Simon & Garfunkel"),
        QStringLiteral("Starving Trancer feat.Saori Hayami"),
        QStringLiteral("Unused Value"),
    };

    // item 1: Role credit
    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    QJsonArray perf1;
    QJsonObject p1;
    p1.insert(QStringLiteral("name"), QStringLiteral("高垣彩陽"));
    p1.insert(QStringLiteral("aka"), QJsonArray());
    perf1.append(p1);
    obj1.insert(QStringLiteral("performers"), perf1);
    QJsonArray roles1;
    roles1.append(QStringLiteral("雪音クリス"));
    obj1.insert(QStringLiteral("roles"), roles1);
    obj1.insert(QStringLiteral("confidence"), 0.95);
    obj1.insert(QStringLiteral("reason"), QStringLiteral("Removed character role"));

    // item 2: Band (1 performer with aka)
    QJsonObject obj2;
    obj2.insert(QStringLiteral("id"), 2);
    QJsonArray perf2;
    QJsonObject p2;
    p2.insert(QStringLiteral("name"), QStringLiteral("Simon & Garfunkel"));
    p2.insert(QStringLiteral("aka"), QJsonArray());
    perf2.append(p2);
    obj2.insert(QStringLiteral("performers"), perf2);
    obj2.insert(QStringLiteral("roles"), QJsonArray());
    obj2.insert(QStringLiteral("confidence"), 0.99);
    obj2.insert(QStringLiteral("reason"), QStringLiteral("Known band"));

    // item 3: Multi-performer collaboration
    QJsonObject obj3;
    obj3.insert(QStringLiteral("id"), 3);
    QJsonArray perf3;
    QJsonObject p3a;
    p3a.insert(QStringLiteral("name"), QStringLiteral("Starving Trancer"));
    p3a.insert(QStringLiteral("aka"), QJsonArray());
    QJsonObject p3b;
    p3b.insert(QStringLiteral("name"), QStringLiteral("Saori Hayami"));
    QJsonArray aka3b;
    aka3b.append(QStringLiteral("早見沙織"));
    p3b.insert(QStringLiteral("aka"), aka3b);
    perf3.append(p3a);
    perf3.append(p3b);
    obj3.insert(QStringLiteral("performers"), perf3);
    obj3.insert(QStringLiteral("roles"), QJsonArray());
    obj3.insert(QStringLiteral("confidence"), 0.9);
    obj3.insert(QStringLiteral("reason"), QStringLiteral("Collaborating artists"));

    // item 4: duplicate id 1 (should be skipped)
    QJsonObject objDup;
    objDup.insert(QStringLiteral("id"), 1);
    QJsonArray perfDup;
    QJsonObject pDup;
    pDup.insert(QStringLiteral("name"), QStringLiteral("Duplicate"));
    pDup.insert(QStringLiteral("aka"), QJsonArray());
    perfDup.append(pDup);
    objDup.insert(QStringLiteral("performers"), perfDup);
    objDup.insert(QStringLiteral("roles"), QJsonArray());
    objDup.insert(QStringLiteral("confidence"), 0.5);
    objDup.insert(QStringLiteral("reason"), QStringLiteral("Duplicate"));

    // item 5: out of bounds id 99 (should be skipped)
    QJsonObject objOob;
    objOob.insert(QStringLiteral("id"), 99);
    QJsonArray perfOob;
    QJsonObject pOob;
    pOob.insert(QStringLiteral("name"), QStringLiteral("OutOfBounds"));
    pOob.insert(QStringLiteral("aka"), QJsonArray());
    perfOob.append(pOob);
    objOob.insert(QStringLiteral("performers"), perfOob);
    objOob.insert(QStringLiteral("roles"), QJsonArray());
    objOob.insert(QStringLiteral("confidence"), 0.5);
    objOob.insert(QStringLiteral("reason"), QStringLiteral("Out of bounds"));

    // item 6: empty performers (should be skipped)
    QJsonObject objEmpty;
    objEmpty.insert(QStringLiteral("id"), 4);
    objEmpty.insert(QStringLiteral("performers"), QJsonArray());
    objEmpty.insert(QStringLiteral("roles"), QJsonArray());
    objEmpty.insert(QStringLiteral("confidence"), 0.5);
    objEmpty.insert(QStringLiteral("reason"), QStringLiteral("Empty performers"));

    QJsonArray itemsArr;
    itemsArr.append(obj1);
    itemsArr.append(obj2);
    itemsArr.append(obj3);
    itemsArr.append(objDup);
    itemsArr.append(objOob);
    itemsArr.append(objEmpty);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseArtistCreditResult(root, values);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 3);
    QVERIFY(map.contains(QStringLiteral("高垣彩陽（as 雪音クリス）")));
    QVERIFY(map.contains(QStringLiteral("Simon & Garfunkel")));
    QVERIFY(map.contains(QStringLiteral("Starving Trancer feat.Saori Hayami")));
    QVERIFY(!map.contains(QStringLiteral("Unused Value")));

    const auto &c1 = map.value(QStringLiteral("高垣彩陽（as 雪音クリス）"));
    QCOMPARE(c1.performers.size(), 1);
    QCOMPARE(c1.performers.at(0).name, QStringLiteral("高垣彩陽"));
    QCOMPARE(c1.roles, QStringList { QStringLiteral("雪音クリス") });
    QCOMPARE(c1.confidence, 0.95);

    const auto &c3 = map.value(QStringLiteral("Starving Trancer feat.Saori Hayami"));
    QCOMPARE(c3.performers.size(), 2);
    QCOMPARE(c3.performers.at(0).name, QStringLiteral("Starving Trancer"));
    QCOMPARE(c3.performers.at(1).name, QStringLiteral("Saori Hayami"));
    QCOMPARE(c3.performers.at(1).aka, QStringList { QStringLiteral("早見沙織") });
}

void TstArtistCreditLlm::omittedValuesGetAsIsResult()
{
    const QStringList values = {
        QStringLiteral("Jay Chou"),
        QStringLiteral("高垣彩陽（as 雪音クリス）"),
        QStringLiteral("IU"),
    };

    // Only return item for id 2 ("高垣彩陽（as 雪音クリス）"); id 1 and id 3 are omitted
    QJsonObject obj2;
    obj2.insert(QStringLiteral("id"), 2);
    QJsonArray perf2;
    QJsonObject p2;
    p2.insert(QStringLiteral("name"), QStringLiteral("高垣彩陽"));
    perf2.append(p2);
    obj2.insert(QStringLiteral("performers"), perf2);
    QJsonArray roles2;
    roles2.append(QStringLiteral("雪音クリス"));
    obj2.insert(QStringLiteral("roles"), roles2);
    obj2.insert(QStringLiteral("confidence"), 0.95);
    obj2.insert(QStringLiteral("reason"), QStringLiteral("Removed character role"));

    QJsonArray itemsArr;
    itemsArr.append(obj2);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseArtistCreditResult(root, values);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 3);
    QVERIFY(map.contains(QStringLiteral("Jay Chou")));
    QVERIFY(map.contains(QStringLiteral("高垣彩陽（as 雪音クリス）")));
    QVERIFY(map.contains(QStringLiteral("IU")));

    // id 1: Omitted -> as-is result
    const auto &c1 = map.value(QStringLiteral("Jay Chou"));
    QCOMPARE(c1.performers.size(), 1);
    QCOMPARE(c1.performers.at(0).name, QStringLiteral("Jay Chou"));
    QVERIFY(c1.performers.at(0).aka.isEmpty());
    QVERIFY(c1.roles.isEmpty());
    QCOMPARE(c1.confidence, 1.0);
    QVERIFY(c1.reason.isEmpty());
    QCOMPARE(normalizedValue(c1), QStringLiteral("Jay Chou"));

    // id 2: Parsed -> explicit result
    const auto &c2 = map.value(QStringLiteral("高垣彩陽（as 雪音クリス）"));
    QCOMPARE(c2.performers.size(), 1);
    QCOMPARE(c2.performers.at(0).name, QStringLiteral("高垣彩陽"));
    QCOMPARE(c2.roles, QStringList { QStringLiteral("雪音クリス") });
    QCOMPARE(c2.confidence, 0.95);
    QCOMPARE(normalizedValue(c2), QStringLiteral("高垣彩陽"));

    // id 3: Omitted -> as-is result
    const auto &c3 = map.value(QStringLiteral("IU"));
    QCOMPARE(c3.performers.size(), 1);
    QCOMPARE(c3.performers.at(0).name, QStringLiteral("IU"));
    QVERIFY(c3.performers.at(0).aka.isEmpty());
    QVERIFY(c3.roles.isEmpty());
    QCOMPARE(c3.confidence, 1.0);
    QVERIFY(c3.reason.isEmpty());
    QCOMPARE(normalizedValue(c3), QStringLiteral("IU"));
}

void TstArtistCreditLlm::emptyItemsArrayGivesAllAsIs()
{
    const QStringList values = {
        QStringLiteral("Artist 1"),
        QStringLiteral("Artist 2"),
    };

    QJsonObject root;
    root.insert(QStringLiteral("items"), QJsonArray());

    const auto res = parseArtistCreditResult(root, values);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 2);
    QVERIFY(map.contains(QStringLiteral("Artist 1")));
    QVERIFY(map.contains(QStringLiteral("Artist 2")));

    const auto &c1 = map.value(QStringLiteral("Artist 1"));
    QCOMPARE(c1.performers.size(), 1);
    QCOMPARE(c1.performers.at(0).name, QStringLiteral("Artist 1"));
    QVERIFY(c1.performers.at(0).aka.isEmpty());
    QVERIFY(c1.roles.isEmpty());
    QCOMPARE(c1.confidence, 1.0);
    QVERIFY(c1.reason.isEmpty());
    QCOMPARE(normalizedValue(c1), QStringLiteral("Artist 1"));

    const auto &c2 = map.value(QStringLiteral("Artist 2"));
    QCOMPARE(c2.performers.size(), 1);
    QCOMPARE(c2.performers.at(0).name, QStringLiteral("Artist 2"));
    QVERIFY(c2.performers.at(0).aka.isEmpty());
    QVERIFY(c2.roles.isEmpty());
    QCOMPARE(c2.confidence, 1.0);
    QVERIFY(c2.reason.isEmpty());
    QCOMPARE(normalizedValue(c2), QStringLiteral("Artist 2"));
}

void TstArtistCreditLlm::optionalAkaAndRolesParsedAsEmpty()
{
    QJsonObject itemObj;
    itemObj.insert(QStringLiteral("id"), 1);

    QJsonArray perfArr;
    QJsonObject pObj;
    pObj.insert(QStringLiteral("name"), QStringLiteral("Solo Artist"));
    // "aka" is omitted
    perfArr.append(pObj);
    itemObj.insert(QStringLiteral("performers"), perfArr);
    // "roles" is omitted
    itemObj.insert(QStringLiteral("confidence"), 0.9);
    itemObj.insert(QStringLiteral("reason"), QStringLiteral("Solo artist without aka or roles"));

    const auto creditOpt = artistCreditFromJson(itemObj);
    QVERIFY(creditOpt.has_value());
    if (!creditOpt.has_value()) {
        return;
    }

    QCOMPARE(creditOpt->performers.size(), 1);
    QCOMPARE(creditOpt->performers.at(0).name, QStringLiteral("Solo Artist"));
    QVERIFY(creditOpt->performers.at(0).aka.isEmpty());
    QVERIFY(creditOpt->roles.isEmpty());
    QCOMPARE(creditOpt->confidence, 0.9);
    QCOMPARE(creditOpt->reason, QStringLiteral("Solo artist without aka or roles"));
}

void TstArtistCreditLlm::parseMissingItemsFails()
{
    const QStringList values = { QStringLiteral("Artist") };
    QJsonObject root;
    root.insert(QStringLiteral("wrong_key"), QJsonArray());

    const auto res = parseArtistCreditResult(root, values);
    QVERIFY(!res.ok());
}

void TstArtistCreditLlm::roundTripAndNormalizedValue()
{
    ArtistCredit credit {
        .performers = {
            CreditPerformer { .name = QStringLiteral("Artist A"), .aka = { QStringLiteral("Aka A") } },
            CreditPerformer { .name = QStringLiteral("Artist B"), .aka = { } },
        },
        .roles = { QStringLiteral("Role 1"), QStringLiteral("Role 2") },
        .confidence = 0.88,
        .reason = QStringLiteral("Two performers with roles"),
    };

    QCOMPARE(normalizedValue(credit), QStringLiteral("Artist A / Artist B"));

    const QJsonObject json = toJson(credit);
    const auto parsedOpt = artistCreditFromJson(json);
    QVERIFY(parsedOpt.has_value());
    if (!parsedOpt.has_value()) {
        return;
    }

    QCOMPARE(*parsedOpt, credit);

    // Invalid json tests: missing performers
    QJsonObject emptyPerformers = json;
    emptyPerformers.insert(QStringLiteral("performers"), QJsonArray());
    QVERIFY(!artistCreditFromJson(emptyPerformers).has_value());

    QJsonObject missingPerformers = json;
    missingPerformers.remove(QStringLiteral("performers"));
    QVERIFY(!artistCreditFromJson(missingPerformers).has_value());

    // Missing confidence or reason
    QJsonObject missingConfidence = json;
    missingConfidence.remove(QStringLiteral("confidence"));
    QVERIFY(!artistCreditFromJson(missingConfidence).has_value());

    QJsonObject missingReason = json;
    missingReason.remove(QStringLiteral("reason"));
    QVERIFY(!artistCreditFromJson(missingReason).has_value());

    // Missing roles or aka is valid (parsed as empty)
    QJsonObject missingRoles = json;
    missingRoles.remove(QStringLiteral("roles"));
    const auto optWithoutRoles = artistCreditFromJson(missingRoles);
    QVERIFY(optWithoutRoles.has_value());
    if (optWithoutRoles.has_value()) {
        QVERIFY(optWithoutRoles->roles.isEmpty());
    }

    // Performer with empty name is invalid
    QJsonObject invalidPerformer = json;
    QJsonArray invalidPerfArr;
    QJsonObject emptyNameP;
    emptyNameP.insert(QStringLiteral("name"), QStringLiteral("   "));
    invalidPerfArr.append(emptyNameP);
    invalidPerformer.insert(QStringLiteral("performers"), invalidPerfArr);
    QVERIFY(!artistCreditFromJson(invalidPerformer).has_value());
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistCreditLlm)

#include "tst_ArtistCreditLlm.moc"
