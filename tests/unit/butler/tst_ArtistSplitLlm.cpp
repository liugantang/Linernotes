// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTest>

#include <butler/ArtistSplit.h>
#include <butler/ArtistSplitLlm.h>
#include <butler/ArtistSplitSource.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/LibraryEnums.h>

namespace {

using linernotes::butler::ArtistSplitCandidate;
using linernotes::butler::ArtistSplitGroup;
using linernotes::butler::parseArtistSplitResult;
using linernotes::butler::SplitDecision;
using linernotes::butler::SplitVerdict;
using linernotes::butler::TrackFieldTarget;
using linernotes::library::CorrectionSource;
using linernotes::library::TagField;

class TstArtistSplitLlm : public QObject {
    Q_OBJECT

private slots:
    void parseValidResult();
    void parseRejectsInventedPart();
};

void TstArtistSplitLlm::parseValidResult()
{
    ArtistSplitCandidate cand0;
    cand0.id = 0;
    cand0.original = QStringLiteral("Starving Trancer feat.Saori Hayami");
    cand0.decision = SplitDecision {
        .verdict = SplitVerdict::Split,
        .parts = { QStringLiteral("Starving Trancer"), QStringLiteral("Saori Hayami") },
        .confidence = 0.85,
    };
    cand0.targets = {
        TrackFieldTarget { .trackId = 10, .field = TagField::Artist },
    };

    ArtistSplitCandidate cand1;
    cand1.id = 1;
    cand1.original = QStringLiteral("Simon & Garfunkel");
    cand1.decision = SplitDecision {
        .verdict = SplitVerdict::Ambiguous,
        .parts = { QStringLiteral("Simon"), QStringLiteral("Garfunkel") },
        .confidence = 0.0,
    };
    cand1.targets = {
        TrackFieldTarget { .trackId = 11, .field = TagField::Artist },
    };

    ArtistSplitGroup group;
    group.candidates = { cand0, cand1 };

    QJsonObject obj0;
    obj0.insert(QStringLiteral("id"), 0);
    QJsonArray parts0;
    parts0.append(QStringLiteral("Starving Trancer"));
    parts0.append(QStringLiteral("Saori Hayami"));
    obj0.insert(QStringLiteral("parts"), parts0);
    obj0.insert(QStringLiteral("confidence"), 0.95);
    obj0.insert(QStringLiteral("reason"), QStringLiteral("Two collaborating artists"));

    QJsonObject obj1;
    obj1.insert(QStringLiteral("id"), 1);
    QJsonArray parts1;
    parts1.append(QStringLiteral("Simon & Garfunkel"));
    obj1.insert(QStringLiteral("parts"), parts1);
    obj1.insert(QStringLiteral("confidence"), 0.99);
    obj1.insert(QStringLiteral("reason"), QStringLiteral("Known band; do not split"));

    QJsonArray itemsArr;
    itemsArr.append(obj0);
    itemsArr.append(obj1);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseArtistSplitResult(root, group);
    QVERIFY(res.ok());
    const auto &proposals = res.value();
    QCOMPARE(proposals.size(), 1);

    const auto &p = proposals.at(0);
    QCOMPARE(p.trackId, 10);
    QCOMPARE(p.field, TagField::Artist);
    QCOMPARE(p.oldValue, QStringLiteral("Starving Trancer feat.Saori Hayami"));
    QCOMPARE(p.newValue, QStringLiteral("Starving Trancer / Saori Hayami"));
    QCOMPARE(p.source, CorrectionSource::Llm);
    QCOMPARE(p.confidence, 0.95);
    QCOMPARE(p.reason, QStringLiteral("Two collaborating artists"));
}

void TstArtistSplitLlm::parseRejectsInventedPart()
{
    ArtistSplitCandidate cand0;
    cand0.id = 0;
    cand0.original = QStringLiteral("Starving Trancer feat.Saori Hayami");
    cand0.targets = {
        TrackFieldTarget { .trackId = 10, .field = TagField::Artist },
    };

    ArtistSplitGroup group;
    group.candidates = { cand0 };

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), 0);
    QJsonArray parts;
    parts.append(QStringLiteral("Completely Invented Artist"));
    parts.append(QStringLiteral("Saori Hayami"));
    obj.insert(QStringLiteral("parts"), parts);
    obj.insert(QStringLiteral("confidence"), 0.9);
    obj.insert(QStringLiteral("reason"), QStringLiteral("Tampered name"));

    QJsonArray itemsArr;
    itemsArr.append(obj);

    QJsonObject root;
    root.insert(QStringLiteral("items"), itemsArr);

    const auto res = parseArtistSplitResult(root, group);
    QVERIFY(!res.ok());
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistSplitLlm)

#include "tst_ArtistSplitLlm.moc"
