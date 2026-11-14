// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <common/ManualClock.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgeService.h>
#include <guide/KnowledgeStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::guide::KnowledgeEntry;
using linernotes::guide::KnowledgeFact;
using linernotes::guide::knowledgeKey;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::KnowledgeQuery;
using linernotes::guide::KnowledgeResult;
using linernotes::guide::KnowledgeService;
using linernotes::guide::KnowledgeStatus;
using linernotes::guide::KnowledgeStore;
using linernotes::guide::WikipediaSummary;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

class TstKnowledgeService : public QObject {
    Q_OBJECT

private slots:
    void cachedFoundReturnsSynchronouslyWithoutNetwork();
    void cachedNotFoundReturnsSynchronouslyWithoutNetwork();
    void emptyArtistReturnsNotFoundSynchronouslyWithoutNetwork();
};

void TstKnowledgeService::cachedFoundReturnsSynchronouslyWithoutNetwork()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("ksvc1.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);
    KnowledgeStore store(db, clock);

    KnowledgeEntry entry;
    entry.kind = KnowledgeKind::Artist;
    entry.qid = QStringLiteral("Q4021857");
    entry.label = QStringLiteral("藍井エイル");
    entry.description = QStringLiteral("Japanese singer");
    entry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q4021857"));
    entry.summary = WikipediaSummary {
        .lang = QStringLiteral("ja"),
        .title = QStringLiteral("藍井エイル"),
        .extract = QStringLiteral("藍井 エイルは、日本の女性歌手。"),
        .pageUrl = QUrl(QStringLiteral(
            "https://ja.wikipedia.org/wiki/%E8%97%8D%E4%BA%95%E3%82%A8%E3%82%A4%E3%83%AB")),
    };
    entry.facts = {
        KnowledgeFact { .property = QStringLiteral("P106"), .value = QStringLiteral("歌手") },
    };
    entry.fetchedAtMs = 1000000000;

    const KnowledgeQuery q {
        .kind = KnowledgeKind::Artist,
        .artist = QStringLiteral("藍井エイル"),
        .title = { },
    };
    const QString key = knowledgeKey(q);
    QVERIFY(store.saveFound(key, entry).ok());

    QNetworkAccessManager nam;
    QSignalSpy namFinishedSpy(&nam, &QNetworkAccessManager::finished);

    KnowledgeService service(db, nam, clock);
    QSignalSpy finishedSpy(&service, &KnowledgeService::finished);

    service.lookup(q);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(namFinishedSpy.count(), 0);

    const auto result = finishedSpy.at(0).at(0).value<KnowledgeResult>();
    QCOMPARE(result.key, key);
    QCOMPARE(result.status, KnowledgeStatus::Found);
    QCOMPARE(result.entry, entry);
}

void TstKnowledgeService::cachedNotFoundReturnsSynchronouslyWithoutNetwork()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("ksvc2.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);
    KnowledgeStore store(db, clock);

    const KnowledgeQuery q {
        .kind = KnowledgeKind::Song,
        .artist = QStringLiteral("Unknown Artist"),
        .title = QStringLiteral("Unknown Song"),
    };
    const QString key = knowledgeKey(q);
    QVERIFY(store.saveNotFound(key).ok());

    QNetworkAccessManager nam;
    QSignalSpy namFinishedSpy(&nam, &QNetworkAccessManager::finished);

    KnowledgeService service(db, nam, clock);
    QSignalSpy finishedSpy(&service, &KnowledgeService::finished);

    service.lookup(q);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(namFinishedSpy.count(), 0);

    const auto result = finishedSpy.at(0).at(0).value<KnowledgeResult>();
    QCOMPARE(result.key, key);
    QCOMPARE(result.status, KnowledgeStatus::NotFound);
}

void TstKnowledgeService::emptyArtistReturnsNotFoundSynchronouslyWithoutNetwork()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("ksvc3.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);

    const KnowledgeQuery q {
        .kind = KnowledgeKind::Artist,
        .artist = QStringLiteral("   "),
        .title = { },
    };

    QNetworkAccessManager nam;
    QSignalSpy namFinishedSpy(&nam, &QNetworkAccessManager::finished);

    KnowledgeService service(db, nam, clock);
    QSignalSpy finishedSpy(&service, &KnowledgeService::finished);

    service.lookup(q);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(namFinishedSpy.count(), 0);

    const auto result = finishedSpy.at(0).at(0).value<KnowledgeResult>();
    QCOMPARE(result.status, KnowledgeStatus::NotFound);
}

} // namespace

QTEST_GUILESS_MAIN(TstKnowledgeService)

#include "tst_KnowledgeService.moc"
