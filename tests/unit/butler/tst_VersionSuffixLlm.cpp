// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTest>

#include <butler/TitleVersion.h>
#include <butler/VersionSuffixLlm.h>
#include <butler/VersionSuffixSource.h>
#include <butler/VersionSuffixStore.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::parseVersionSuffixResult;
using linernotes::butler::SuffixRole;
using linernotes::butler::SuffixSample;
using linernotes::library::VersionType;

class TstVersionSuffixLlm : public QObject {
    Q_OBJECT

private slots:
    void parseValidThreeItems();
    void skipMissingTypeForVersion();
    void skipOutOfBoundsAndDuplicateId();
    void parseNonObjectOrMissingItemsFails();
};

void TstVersionSuffixLlm::parseValidThreeItems()
{
    const QList<SuffixSample> samples = {
        SuffixSample {
            .key = QStringLiteral("maki mix"),
            .suffix = QStringLiteral("Maki Mix"),
            .title = QStringLiteral("Snow halation (Maki Mix)"),
        },
        SuffixSample {
            .key = QStringLiteral("elite"),
            .suffix = QStringLiteral("Elite"),
            .title = QStringLiteral("Song (Elite)"),
        },
        SuffixSample {
            .key = QStringLiteral("sengoku nadeko"),
            .suffix = QStringLiteral("千石撫子"),
            .title = QStringLiteral("恋爱サーキュレーション (千石撫子)"),
        },
    };

    // item 1: version / remix
    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    obj1.insert(QStringLiteral("role"), QStringLiteral("version"));
    obj1.insert(QStringLiteral("type"), QStringLiteral("remix"));
    obj1.insert(QStringLiteral("confidence"), 0.95);
    obj1.insert(QStringLiteral("reason"), QStringLiteral("Remix version"));

    // item 2: title_part
    QJsonObject obj2;
    obj2.insert(QStringLiteral("id"), 2);
    obj2.insert(QStringLiteral("role"), QStringLiteral("title_part"));
    obj2.insert(QStringLiteral("confidence"), 0.90);
    obj2.insert(QStringLiteral("reason"), QStringLiteral("Title part"));

    // item 3: annotation
    QJsonObject obj3;
    obj3.insert(QStringLiteral("id"), 3);
    obj3.insert(QStringLiteral("role"), QStringLiteral("annotation"));
    obj3.insert(QStringLiteral("confidence"), 0.85);
    obj3.insert(QStringLiteral("reason"), QStringLiteral("Character credit"));

    QJsonArray itemsArr;
    itemsArr.append(obj1);
    itemsArr.append(obj2);
    itemsArr.append(obj3);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseVersionSuffixResult(root, samples);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 3);
    QVERIFY(map.contains(QStringLiteral("maki mix")));
    QVERIFY(map.contains(QStringLiteral("elite")));
    QVERIFY(map.contains(QStringLiteral("sengoku nadeko")));

    const auto &v1 = map.value(QStringLiteral("maki mix"));
    QCOMPARE(v1.cls.role, SuffixRole::Version);
    QCOMPARE(v1.cls.type, VersionType::Remix);
    QCOMPARE(v1.confidence, 0.95);
    QCOMPARE(v1.reason, QStringLiteral("Remix version"));

    const auto &v2 = map.value(QStringLiteral("elite"));
    QCOMPARE(v2.cls.role, SuffixRole::TitlePart);
    QCOMPARE(v2.cls.type, VersionType::Studio);
    QCOMPARE(v2.confidence, 0.90);
    QCOMPARE(v2.reason, QStringLiteral("Title part"));

    const auto &v3 = map.value(QStringLiteral("sengoku nadeko"));
    QCOMPARE(v3.cls.role, SuffixRole::Annotation);
    QCOMPARE(v3.cls.type, VersionType::Studio);
    QCOMPARE(v3.confidence, 0.85);
    QCOMPARE(v3.reason, QStringLiteral("Character credit"));
}

void TstVersionSuffixLlm::skipMissingTypeForVersion()
{
    const QList<SuffixSample> samples = {
        SuffixSample {
            .key = QStringLiteral("ver a"),
            .suffix = QStringLiteral("Ver A"),
            .title = QStringLiteral("Song (Ver A)"),
        },
        SuffixSample {
            .key = QStringLiteral("ver b"),
            .suffix = QStringLiteral("Ver B"),
            .title = QStringLiteral("Song (Ver B)"),
        },
    };

    // item 1: role=version but missing type -> should be skipped
    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    obj1.insert(QStringLiteral("role"), QStringLiteral("version"));
    // missing "type"
    obj1.insert(QStringLiteral("confidence"), 0.90);
    obj1.insert(QStringLiteral("reason"), QStringLiteral("Version missing type"));

    // item 2: role=version with valid type -> should succeed
    QJsonObject obj2;
    obj2.insert(QStringLiteral("id"), 2);
    obj2.insert(QStringLiteral("role"), QStringLiteral("version"));
    obj2.insert(QStringLiteral("type"), QStringLiteral("alternate"));
    obj2.insert(QStringLiteral("confidence"), 0.95);
    obj2.insert(QStringLiteral("reason"), QStringLiteral("Alternate version"));

    QJsonArray itemsArr;
    itemsArr.append(obj1);
    itemsArr.append(obj2);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseVersionSuffixResult(root, samples);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 1);
    QVERIFY(!map.contains(QStringLiteral("ver a")));
    QVERIFY(map.contains(QStringLiteral("ver b")));

    const auto &v2 = map.value(QStringLiteral("ver b"));
    QCOMPARE(v2.cls.role, SuffixRole::Version);
    QCOMPARE(v2.cls.type, VersionType::Alternate);
}

void TstVersionSuffixLlm::skipOutOfBoundsAndDuplicateId()
{
    const QList<SuffixSample> samples = {
        SuffixSample {
            .key = QStringLiteral("sample 1"),
            .suffix = QStringLiteral("Suffix 1"),
            .title = QStringLiteral("Song (Suffix 1)"),
        },
    };

    // item 1: id=99 (out of bounds) -> skip
    QJsonObject objOob;
    objOob.insert(QStringLiteral("id"), 99);
    objOob.insert(QStringLiteral("role"), QStringLiteral("title_part"));
    objOob.insert(QStringLiteral("confidence"), 0.9);
    objOob.insert(QStringLiteral("reason"), QStringLiteral("Out of bounds"));

    // item 2: valid id=1
    QJsonObject objValid;
    objValid.insert(QStringLiteral("id"), 1);
    objValid.insert(QStringLiteral("role"), QStringLiteral("annotation"));
    objValid.insert(QStringLiteral("confidence"), 0.95);
    objValid.insert(QStringLiteral("reason"), QStringLiteral("Valid"));

    // item 3: duplicate id=1 -> skip
    QJsonObject objDup;
    objDup.insert(QStringLiteral("id"), 1);
    objDup.insert(QStringLiteral("role"), QStringLiteral("title_part"));
    objDup.insert(QStringLiteral("confidence"), 0.5);
    objDup.insert(QStringLiteral("reason"), QStringLiteral("Duplicate"));

    QJsonArray itemsArr;
    itemsArr.append(objOob);
    itemsArr.append(objValid);
    itemsArr.append(objDup);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseVersionSuffixResult(root, samples);
    QVERIFY(res.ok());
    const auto &map = res.value();

    QCOMPARE(map.size(), 1);
    QVERIFY(map.contains(QStringLiteral("sample 1")));
    QCOMPARE(map.value(QStringLiteral("sample 1")).cls.role, SuffixRole::Annotation);
}

void TstVersionSuffixLlm::parseNonObjectOrMissingItemsFails()
{
    const QList<SuffixSample> samples = {
        SuffixSample {
            .key = QStringLiteral("sample 1"),
            .suffix = QStringLiteral("Suffix 1"),
            .title = QStringLiteral("Song (Suffix 1)"),
        },
    };

    // Non-object
    const auto res1
        = parseVersionSuffixResult(QJsonValue(QStringLiteral("not an object")), samples);
    QVERIFY(!res1.ok());

    // Missing "items"
    QJsonObject emptyObj;
    const auto res2 = parseVersionSuffixResult(emptyObj, samples);
    QVERIFY(!res2.ok());

    // "items" is not an array
    QJsonObject invalidItemsObj;
    invalidItemsObj.insert(QStringLiteral("items"), QStringLiteral("not array"));
    const auto res3 = parseVersionSuffixResult(invalidItemsObj, samples);
    QVERIFY(!res3.ok());
}

} // namespace

QTEST_GUILESS_MAIN(TstVersionSuffixLlm)

#include "tst_VersionSuffixLlm.moc"
