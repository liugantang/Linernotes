// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <common/ManualClock.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgeStore.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::guide::KnowledgeEntry;
using linernotes::guide::KnowledgeFact;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::KnowledgeStore;
using linernotes::guide::LookupState;
using linernotes::guide::WikipediaSummary;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

class TstKnowledgeStore : public QObject {
    Q_OBJECT

private slots:
    void saveFoundAndGetFound();
    void foundExpiresAfter180Days();
    void notFoundExpiresAfter30Days();
};

void TstKnowledgeStore::saveFoundAndGetFound()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("kstore1.db")));
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

    const QString key = QStringLiteral("artist|aoi eir");
    QVERIFY(store.saveFound(key, entry).ok());

    const auto lookupRes = store.get(key);
    QVERIFY(lookupRes.ok());
    const auto &cached = lookupRes.value();
    QCOMPARE(cached.state, LookupState::Found);
    QCOMPARE(cached.entry, entry);
}

void TstKnowledgeStore::foundExpiresAfter180Days()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("kstore2.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);
    KnowledgeStore store(db, clock);

    KnowledgeEntry entry;
    entry.kind = KnowledgeKind::Song;
    entry.qid = QStringLiteral("Q17988679");
    entry.label = QStringLiteral("IGNITE");
    entry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q17988679"));
    entry.fetchedAtMs = 1000000000;

    const QString key = QStringLiteral("song|aoi eir|ignite");
    QVERIFY(store.saveFound(key, entry).ok());

    // 179 days later -> still found
    clock.advance(179LL * 24 * 60 * 60 * 1000);
    auto res = store.get(key);
    QVERIFY(res.ok());
    QCOMPARE(res.value().state, LookupState::Found);
    QCOMPARE(res.value().entry, entry);

    // 181 days later -> missing (expired)
    clock.advance(2LL * 24 * 60 * 60 * 1000);
    res = store.get(key);
    QVERIFY(res.ok());
    QCOMPARE(res.value().state, LookupState::Missing);
}

void TstKnowledgeStore::notFoundExpiresAfter30Days()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("kstore3.db")));
    QVERIFY(db.open(Migrator()).ok());

    ManualClock clock(1000000000);
    KnowledgeStore store(db, clock);

    const QString key = QStringLiteral("artist|nonexistent");
    QVERIFY(store.saveNotFound(key).ok());

    // 29 days later -> not found
    clock.advance(29LL * 24 * 60 * 60 * 1000);
    auto res = store.get(key);
    QVERIFY(res.ok());
    QCOMPARE(res.value().state, LookupState::NotFound);

    // 31 days later -> missing (expired)
    clock.advance(2LL * 24 * 60 * 60 * 1000);
    res = store.get(key);
    QVERIFY(res.ok());
    QCOMPARE(res.value().state, LookupState::Missing);
}

} // namespace

QTEST_GUILESS_MAIN(TstKnowledgeStore)

#include "tst_KnowledgeStore.moc"
