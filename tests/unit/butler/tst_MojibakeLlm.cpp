// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTest>

#include <butler/Mojibake.h>
#include <butler/MojibakeAnalysis.h>
#include <butler/MojibakeLlm.h>
#include <butler/MojibakeSource.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::AmbiguousItem;
using linernotes::butler::DecodeCandidate;
using linernotes::butler::MojibakeGroup;
using linernotes::butler::mojibakePromptVars;
using linernotes::butler::MojibakeTrack;
using linernotes::butler::parseMojibakeResult;
using linernotes::butler::SourceEncoding;
using linernotes::library::CorrectionSource;
using linernotes::library::TagField;

class TstMojibakeLlm : public QObject {
    Q_OBJECT

private slots:
    void promptVarsContainCandidates();
    void parseValidResult();
    void parseRejectsUnknownId();
};

void TstMojibakeLlm::promptVarsContainCandidates()
{
    MojibakeTrack track;
    track.trackId = 1;
    track.readable = {
        { .field = TagField::Album, .value = QStringLiteral("测试专辑"), .rawBytes = std::nullopt },
    };

    MojibakeGroup group;
    group.directory = QStringLiteral("/home/music/test_album");
    group.tracks = { track };

    AmbiguousItem item;
    item.id = 0;
    item.trackId = 1;
    item.field = TagField::Title;
    item.original = QStringLiteral("å¤©ç©º");
    item.candidates = {
        DecodeCandidate {
            .encoding = SourceEncoding::Utf8,
            .text = QStringLiteral("天空"),
            .score = 0.85,
        },
        DecodeCandidate {
            .encoding = SourceEncoding::Gbk,
            .text = QStringLiteral("未知"),
            .score = 0.40,
        },
    };

    const auto vars = mojibakePromptVars(group, { item });

    QVERIFY(vars.contains(QStringLiteral("directory")));
    QCOMPARE(vars.value(QStringLiteral("directory")), QStringLiteral("/home/music/test_album"));

    QVERIFY(vars.contains(QStringLiteral("context")));
    QVERIFY(vars.value(QStringLiteral("context")).contains(QStringLiteral("测试专辑")));

    QVERIFY(vars.contains(QStringLiteral("items")));
    const QString itemsText = vars.value(QStringLiteral("items"));
    QVERIFY(itemsText.contains(QStringLiteral("天空")));
    QVERIFY(itemsText.contains(QStringLiteral("utf-8")));
}

void TstMojibakeLlm::parseValidResult()
{
    AmbiguousItem item0;
    item0.id = 0;
    item0.trackId = 10;
    item0.field = TagField::Title;
    item0.original = QStringLiteral("å¤©ç©º");

    AmbiguousItem item1;
    item1.id = 1;
    item1.trackId = 11;
    item1.field = TagField::Artist;
    item1.original = QStringLiteral("æž—æ™“é£Ž");

    QJsonObject obj0;
    obj0.insert(QStringLiteral("id"), 0);
    obj0.insert(QStringLiteral("text"), QStringLiteral("天空"));
    obj0.insert(QStringLiteral("confidence"), 0.95);
    obj0.insert(QStringLiteral("reason"), QStringLiteral("UTF-8 解码符合语境"));

    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    obj1.insert(QStringLiteral("text"), QJsonValue(QJsonValue::Null));
    obj1.insert(QStringLiteral("confidence"), 0.0);
    obj1.insert(QStringLiteral("reason"), QStringLiteral("无法判断"));

    QJsonArray itemsArr;
    itemsArr.append(obj0);
    itemsArr.append(obj1);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseMojibakeResult(root, { item0, item1 });
    QVERIFY(res.ok());
    const auto &proposals = res.value();
    QCOMPARE(proposals.size(), 1);

    const auto &p = proposals.at(0);
    QCOMPARE(p.trackId, 10);
    QCOMPARE(p.field, TagField::Title);
    QCOMPARE(p.oldValue, QStringLiteral("å¤©ç©º"));
    QCOMPARE(p.newValue, QStringLiteral("天空"));
    QCOMPARE(p.source, CorrectionSource::Llm);
    QCOMPARE(p.confidence, 0.95);
    QCOMPARE(p.reason, QStringLiteral("UTF-8 解码符合语境"));
}

void TstMojibakeLlm::parseRejectsUnknownId()
{
    AmbiguousItem item0;
    item0.id = 0;
    item0.trackId = 10;
    item0.field = TagField::Title;
    item0.original = QStringLiteral("å¤©ç©º");

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), 999);
    obj.insert(QStringLiteral("text"), QStringLiteral("天空"));
    obj.insert(QStringLiteral("confidence"), 0.9);
    obj.insert(QStringLiteral("reason"), QStringLiteral("测试未知 ID"));

    QJsonArray itemsArr;
    itemsArr.append(obj);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseMojibakeResult(root, { item0 });
    QVERIFY(!res.ok());
}

} // namespace

QTEST_GUILESS_MAIN(TstMojibakeLlm)

#include "tst_MojibakeLlm.moc"
