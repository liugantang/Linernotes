// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <common/ManualClock.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgeStore.h>
#include <guide/TrackFacts.h>
#include <guide/Wikidata.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::guide::cachedTrackKnowledge;
using linernotes::guide::factPropertyName;
using linernotes::guide::KnowledgeEntry;
using linernotes::guide::KnowledgeFact;
using linernotes::guide::knowledgeKey;
using linernotes::guide::KnowledgeKind;
using linernotes::guide::KnowledgeQuery;
using linernotes::guide::KnowledgeStore;
using linernotes::guide::trackFacts;
using linernotes::guide::WikipediaSummary;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;

class TstTrackFacts : public QObject {
    Q_OBJECT

private slots:
    void cachedTrackKnowledgeAndTrackFacts();
};

void TstTrackFacts::cachedTrackKnowledgeAndTrackFacts()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("tfacts.db")));
    QVERIFY(db.open(Migrator()).ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO library_roots (id, path, enabled, added_at) VALUES (1, '/music', 1, 1);")));
    const qint64 rootId = q.lastInsertId().toLongLong();

    q.prepare(
        QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                       "first_seen_at, scanned_at) VALUES (?, ?, 1000, 1000, 200000, 1, 1);"));
    q.addBindValue(rootId);
    q.addBindValue(QStringLiteral("/music/song.flac"));
    QVERIFY(q.exec());
    const qint64 fileId = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                             "VALUES (?, 1, 1);"));
    q.addBindValue(fileId);
    QVERIFY(q.exec());
    const qint64 trackId = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral("UPDATE effective_metadata SET title = ?, artist = ?, album = ? "
                             "WHERE track_id = ?;"));
    q.addBindValue(QStringLiteral("IGNITE"));
    q.addBindValue(QStringLiteral("藍井エイル"));
    q.addBindValue(QStringLiteral("D'AZUR"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());

    ManualClock clock(1000000000);
    KnowledgeStore store(db, clock);

    KnowledgeEntry artistEntry;
    artistEntry.kind = KnowledgeKind::Artist;
    artistEntry.qid = QStringLiteral("Q4021857");
    artistEntry.label = QStringLiteral("藍井エイル");
    artistEntry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q4021857"));
    artistEntry.summary = WikipediaSummary {
        .lang = QStringLiteral("ja"),
        .title = QStringLiteral("藍井エイル"),
        .extract = QStringLiteral("藍井 エイルは、日本の女性歌手。"),
        .pageUrl = QUrl(QStringLiteral("https://ja.wikipedia.org/wiki/藍井エイル")),
    };
    artistEntry.facts = {
        KnowledgeFact { .property = QStringLiteral("P106"), .value = QStringLiteral("歌手") },
    };
    artistEntry.fetchedAtMs = 1000000000;

    const KnowledgeQuery artistQ {
        .kind = KnowledgeKind::Artist,
        .artist = QStringLiteral("藍井エイル"),
        .title = { },
    };
    QVERIFY(store.saveFound(knowledgeKey(artistQ), artistEntry).ok());

    KnowledgeEntry albumEntry;
    albumEntry.kind = KnowledgeKind::Album;
    albumEntry.qid = QStringLiteral("Q20038843");
    albumEntry.label = QStringLiteral("D'AZUR");
    albumEntry.wikidataUrl = QUrl(QStringLiteral("https://www.wikidata.org/wiki/Q20038843"));
    albumEntry.summary = WikipediaSummary {
        .lang = QStringLiteral("ja"),
        .title = QStringLiteral("D'AZUR"),
        .extract = QStringLiteral("D'AZURは藍井エイルの3枚目のオリジナルアルバム。"),
        .pageUrl = QUrl(QStringLiteral("https://ja.wikipedia.org/wiki/D%27AZUR")),
    };
    albumEntry.facts = {
        KnowledgeFact { .property = QStringLiteral("P577"), .value = QStringLiteral("2015-06-24") },
    };
    albumEntry.fetchedAtMs = 1000000000;

    const KnowledgeQuery albumQ {
        .kind = KnowledgeKind::Album,
        .artist = QStringLiteral("藍井エイル"),
        .title = QStringLiteral("D'AZUR"),
    };
    QVERIFY(store.saveFound(knowledgeKey(albumQ), albumEntry).ok());

    const auto knowRes = cachedTrackKnowledge(db, clock, trackId);
    QVERIFY(knowRes.ok());
    const auto &knowledge = knowRes.value();

    QVERIFY(!knowledge.song.has_value());
    QVERIFY(knowledge.album.has_value());
    QVERIFY(knowledge.artist.has_value());
    QCOMPARE(knowledge.album, std::optional<KnowledgeEntry>(albumEntry));
    QCOMPARE(knowledge.artist, std::optional<KnowledgeEntry>(artistEntry));

    const auto facts = trackFacts(knowledge);
    QCOMPARE(facts.size(), 4);

    QCOMPARE(facts.at(0).kind, KnowledgeKind::Album);
    QCOMPARE(facts.at(0).subject, QStringLiteral("D'AZUR"));
    QCOMPARE(facts.at(0).text,
        factPropertyName(QStringLiteral("P577")) + QStringLiteral("：2015-06-24"));
    QCOMPARE(facts.at(0).sourceUrl, albumEntry.wikidataUrl);

    QCOMPARE(facts.at(1).kind, KnowledgeKind::Album);
    QCOMPARE(facts.at(1).subject, QStringLiteral("D'AZUR"));
    QCOMPARE(facts.at(1).text, albumEntry.summary->extract);
    QCOMPARE(facts.at(1).sourceUrl, albumEntry.summary->pageUrl);

    QCOMPARE(facts.at(2).kind, KnowledgeKind::Artist);
    QCOMPARE(facts.at(2).subject, QStringLiteral("藍井エイル"));
    QCOMPARE(facts.at(2).text, factPropertyName(QStringLiteral("P106")) + QStringLiteral("：歌手"));
    QCOMPARE(facts.at(2).sourceUrl, artistEntry.wikidataUrl);

    QCOMPARE(facts.at(3).kind, KnowledgeKind::Artist);
    QCOMPARE(facts.at(3).subject, QStringLiteral("藍井エイル"));
    QCOMPARE(facts.at(3).text, artistEntry.summary->extract);
    QCOMPARE(facts.at(3).sourceUrl, artistEntry.summary->pageUrl);
}

} // namespace

QTEST_GUILESS_MAIN(TstTrackFacts)

#include "tst_TrackFacts.moc"
