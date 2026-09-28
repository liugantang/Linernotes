// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QObject>
#include <QTest>

#include <ai/Errors.h>
#include <ai/ModelList.h>
#include <common/TestSupport.h>

#include <algorithm>

using linernotes::ai::parseModelList;
namespace errc = linernotes::ai::errc;

namespace {

class TstModelList : public QObject {
    Q_OBJECT

private slots:
    void parseRealOllamaCloudFixture();
    void deduplicateAndEmptyId();
    void invalidJsonAndMissingData();
};

void TstModelList::parseRealOllamaCloudFixture()
{
    const QString path
        = linernotes::test::fixturePath(QStringLiteral("ai/models_ollama_cloud.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray json = file.readAll();

    const auto res = parseModelList(json);
    QVERIFY(res.ok());
    const QStringList &models = res.value();
    QCOMPARE(models.size(), 17);
    QVERIFY(models.contains(QStringLiteral("gpt-oss:120b")));

    QStringList sortedModels = models;
    std::ranges::sort(sortedModels, [](const QString &a, const QString &b) {
        const int cmp = a.compare(b, Qt::CaseInsensitive);
        if (cmp != 0) {
            return cmp < 0;
        }
        return a < b;
    });
    QCOMPARE(models, sortedModels);
}

void TstModelList::deduplicateAndEmptyId()
{
    const QByteArray json = R"({
        "object": "list",
        "data": [
            {"id": "model-b"},
            {"id": ""},
            {"id": "   "},
            {"id": "model-a"},
            {"id": "model-b"},
            {"id": "MODEL-A"},
            {"id": "model-c"}
        ]
    })";

    const auto res = parseModelList(json);
    QVERIFY(res.ok());
    const QStringList &models = res.value();
    QVERIFY(!models.contains(QString()));
    QVERIFY(!models.contains(QStringLiteral("   ")));
    QCOMPARE(models.count(QStringLiteral("model-b")), 1);

    const QByteArray emptyJson = R"({"object": "list", "data": []})";
    const auto emptyRes = parseModelList(emptyJson);
    QVERIFY(emptyRes.ok());
    QVERIFY(emptyRes.value().isEmpty());
}

void TstModelList::invalidJsonAndMissingData()
{
    const auto res1 = parseModelList("not json");
    QVERIFY(!res1.ok());
    QCOMPARE(res1.error().code, QString(errc::kBadResponse));

    const auto res2 = parseModelList(R"({"object": "list"})");
    QVERIFY(!res2.ok());
    QCOMPARE(res2.error().code, QString(errc::kBadResponse));

    const auto res3 = parseModelList(R"({"object": "list", "data": "not array"})");
    QVERIFY(!res3.ok());
    QCOMPARE(res3.error().code, QString(errc::kBadResponse));

    const auto res4 = parseModelList(R"([{"id": "foo"}])");
    QVERIFY(!res4.ok());
    QCOMPARE(res4.error().code, QString(errc::kBadResponse));
}

} // namespace

QTEST_GUILESS_MAIN(TstModelList)

#include "tst_ModelList.moc"
