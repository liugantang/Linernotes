// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <library/ArtistAliasCorrections.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Errors.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::ArtistAliasProposal;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionSource;
using linernotes::library::CorrectionStatus;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
namespace errc = linernotes::library::errc;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, NULL, NULL, 0, 3000);"));
        q.addBindValue(fileId);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static bool insertRawTag(
        const QSqlDatabase &db, qint64 trackId, const QString &key, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(key);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }

    static qint64 getArtistId(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?;"));
        q.addBindValue(name);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    }

    static QList<qint64> getTrackArtistIds(const QSqlDatabase &db, qint64 trackId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT artist_id FROM track_artists WHERE track_id = ? ORDER BY position;"));
        q.addBindValue(trackId);
        QList<qint64> ids;
        if (q.exec()) {
            while (q.next()) {
                ids.append(q.value(0).toLongLong());
            }
        }
        return ids;
    }
};

struct ScenarioSetup {
    qint64 t1 = 0;
    qint64 t2 = 0;
    qint64 t3 = 0;
    qint64 t4 = 0;
    qint64 jayId = 0;
    qint64 jayChouId = 0;
    qint64 feiId = 0;
};

ScenarioSetup setupStandardTracks(const QSqlDatabase &conn)
{
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));
    const qint64 f4 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/4.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    const qint64 t4 = TestDbHelper::insertTrack(conn, f4);

    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Track 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("周杰伦"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Track 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("周杰伦"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("Track 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Jay Chou"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    TestDbHelper::insertRawTag(conn, t4, QStringLiteral("TITLE"), QStringLiteral("Track 4"));
    TestDbHelper::insertRawTag(
        conn, t4, QStringLiteral("ARTIST"), QStringLiteral("Jay Chou / 费玉清"));
    TestDbHelper::updateTagsReadAt(conn, t4);

    EntityLinker linker(conn);
    static_cast<void>(linker.linkTrack(t1));
    static_cast<void>(linker.linkTrack(t2));
    static_cast<void>(linker.linkTrack(t3));
    static_cast<void>(linker.linkTrack(t4));

    ScenarioSetup setup;
    setup.t1 = t1;
    setup.t2 = t2;
    setup.t3 = t3;
    setup.t4 = t4;
    setup.jayId = TestDbHelper::getArtistId(conn, QStringLiteral("周杰伦"));
    setup.jayChouId = TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou"));
    setup.feiId = TestDbHelper::getArtistId(conn, QStringLiteral("费玉清"));
    return setup;
}

class TstArtistAliasCorrections : public QObject {
    Q_OBJECT

private slots:
    void acceptMergesVariantEntity();
    void acceptMovesFavoritesAndAliases();
    void revertRestoresVariantEntity();
    void localeAliasDoesNotNeedVariantEntity();
    void invalidProposalsRejectedAsWhole();
    void batchCountsIncludeAliasCorrections();
};

void TstArtistAliasCorrections::acceptMergesVariantEntity()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_merge.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const auto setup = setupStandardTracks(conn);
    QVERIFY(setup.jayId > 0);
    QVERIFY(setup.jayChouId > 0);
    QVERIFY(setup.feiId > 0);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(
        CorrectionKind::ArtistMerge, QStringLiteral("Merge Jay Chou into 周杰伦"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    ArtistAliasProposal p;
    p.canonicalArtistId = setup.jayId;
    p.alias = QStringLiteral("Jay Chou");
    p.source = CorrectionSource::Rule;
    p.confidence = 0.9;
    p.reason = QStringLiteral("Variant spelling");

    const auto addRes = store.addArtistAliasProposals(batchId, { p });
    QVERIFY(addRes.ok());

    const auto rowsRes = store.artistAliasCorrections(batchId);
    QVERIFY(rowsRes.ok());
    QCOMPARE(rowsRes.value().size(), 1);
    const auto &row = rowsRes.value().first();
    QCOMPARE(row.artistId, setup.jayId);
    QCOMPARE(row.artistName, QStringLiteral("周杰伦"));
    QCOMPARE(row.alias, QStringLiteral("Jay Chou"));
    QCOMPARE(row.status, CorrectionStatus::Pending);

    const auto acceptRes = store.accept({ row.id });
    QVERIFY(acceptRes.ok());

    // Jay Chou entity does not exist
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou")), -1);
    // 周杰伦 entity exists
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("周杰伦")), setup.jayId);

    // Track 3 points to 周杰伦
    const auto t3Artists = TestDbHelper::getTrackArtistIds(conn, setup.t3);
    QCOMPARE(t3Artists.size(), 1);
    QCOMPARE(t3Artists.first(), setup.jayId);

    // Track 4 points to 周杰伦 and 费玉清
    const auto t4Artists = TestDbHelper::getTrackArtistIds(conn, setup.t4);
    QCOMPARE(t4Artists.size(), 2);
    QCOMPARE(t4Artists.at(0), setup.jayId);
    QCOMPARE(t4Artists.at(1), setup.feiId);

    // artist_aliases has one row with kind='variant'
    QSqlQuery aliasQuery(conn);
    aliasQuery.prepare(QStringLiteral(
        "SELECT artist_id, alias, kind, source FROM artist_aliases WHERE artist_id = ?;"));
    aliasQuery.addBindValue(setup.jayId);
    QVERIFY(aliasQuery.exec());
    QVERIFY(aliasQuery.next());
    QCOMPARE(aliasQuery.value(1).toString(), QStringLiteral("Jay Chou"));
    QCOMPARE(aliasQuery.value(2).toString(), QStringLiteral("variant"));
    QCOMPARE(aliasQuery.value(3).toString(), QStringLiteral("rule"));
    QVERIFY(!aliasQuery.next());
}

void TstArtistAliasCorrections::acceptMovesFavoritesAndAliases()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_favs.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const auto setup = setupStandardTracks(conn);
    QVERIFY(setup.jayId > 0);
    QVERIFY(setup.jayChouId > 0);

    // Jay Chou entity is favorited and has alias 'JAY'
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('artist', %1, 5000);")
            .arg(setup.jayChouId)));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO artist_aliases (artist_id, alias, kind, source, "
                                  "created_at) VALUES (%1, 'JAY', 'variant', 'user', 6000);")
            .arg(setup.jayChouId)));

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("Merge"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    ArtistAliasProposal p;
    p.canonicalArtistId = setup.jayId;
    p.alias = QStringLiteral("Jay Chou");
    p.source = CorrectionSource::Rule;
    p.confidence = 0.9;

    QVERIFY(store.addArtistAliasProposals(batchId, { p }).ok());
    const auto rows = store.artistAliasCorrections(batchId).value();
    QCOMPARE(rows.size(), 1);
    QVERIFY(store.accept({ rows.first().id }).ok());

    // Favorites: 周杰伦 has favorite with created_at 5000, Jay Chou has 0
    QVERIFY(q.exec(QStringLiteral(
        "SELECT created_at FROM favorites WHERE entity_type = 'artist' AND entity_id = %1;")
            .arg(setup.jayId)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), 5000);

    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM favorites WHERE entity_type = 'artist' AND entity_id = %1;")
            .arg(setup.jayChouId)));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // Aliases: both 'JAY' and 'Jay Chou' belong to 周杰伦
    QVERIFY(q.exec(QStringLiteral("SELECT artist_id FROM artist_aliases WHERE alias = 'JAY';")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), setup.jayId);

    QVERIFY(
        q.exec(QStringLiteral("SELECT artist_id FROM artist_aliases WHERE alias = 'Jay Chou';")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toLongLong(), setup.jayId);
}

void TstArtistAliasCorrections::revertRestoresVariantEntity()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_revert.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const auto setup = setupStandardTracks(conn);
    QVERIFY(setup.jayId > 0);
    QVERIFY(setup.jayChouId > 0);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("Merge"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    ArtistAliasProposal p;
    p.canonicalArtistId = setup.jayId;
    p.alias = QStringLiteral("Jay Chou");
    p.source = CorrectionSource::Rule;
    p.confidence = 0.9;

    QVERIFY(store.addArtistAliasProposals(batchId, { p }).ok());
    const auto rows = store.artistAliasCorrections(batchId).value();
    QCOMPARE(rows.size(), 1);
    QVERIFY(store.accept({ rows.first().id }).ok());

    // Verify merged
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou")), -1);

    // Revert batch
    clock.advance(100);
    const auto revRes = store.revertBatch(batchId);
    QVERIFY(revRes.ok());

    // Jay Chou entity is restored
    const qint64 restoredJayChouId = TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou"));
    QVERIFY(restoredJayChouId > 0);
    QVERIFY(restoredJayChouId != setup.jayId);

    // Track 3 is associated with restored Jay Chou
    const auto t3Artists = TestDbHelper::getTrackArtistIds(conn, setup.t3);
    QCOMPARE(t3Artists.size(), 1);
    QCOMPARE(t3Artists.first(), restoredJayChouId);

    // Alias row deleted
    QSqlQuery q(conn);
    QVERIFY(
        q.exec(QStringLiteral("SELECT COUNT(*) FROM artist_aliases WHERE alias = 'Jay Chou';")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // Status is reverted
    const auto updatedRows = store.artistAliasCorrections(batchId).value();
    QCOMPARE(updatedRows.size(), 1);
    QCOMPARE(updatedRows.first().status, CorrectionStatus::Reverted);
}

void TstArtistAliasCorrections::localeAliasDoesNotNeedVariantEntity()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_locale.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Track 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("周杰伦"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());

    const qint64 jayId = TestDbHelper::getArtistId(conn, QStringLiteral("周杰伦"));
    QVERIFY(jayId > 0);
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou")), -1);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes
        = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("EN Alias"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    ArtistAliasProposal p;
    p.canonicalArtistId = jayId;
    p.alias = QStringLiteral("Jay Chou");
    p.locale = QStringLiteral("en");
    p.source = CorrectionSource::MusicBrainz;
    p.confidence = 1.0;
    p.reason = QStringLiteral("English name");

    QVERIFY(store.addArtistAliasProposals(batchId, { p }).ok());
    const auto rows = store.artistAliasCorrections(batchId).value();
    QCOMPARE(rows.size(), 1);
    QVERIFY(store.accept({ rows.first().id }).ok());

    // Verify artist_aliases row: kind='translation', locale='en'
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "SELECT artist_id, alias, locale, kind, source FROM artist_aliases WHERE artist_id = %1;")
            .arg(jayId)));
    QVERIFY(q.next());
    QCOMPARE(q.value(1).toString(), QStringLiteral("Jay Chou"));
    QCOMPARE(q.value(2).toString(), QStringLiteral("en"));
    QCOMPARE(q.value(3).toString(), QStringLiteral("translation"));
    QCOMPARE(q.value(4).toString(), QStringLiteral("musicbrainz"));

    // Now insert a new track with artist 'Jay Chou'
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Track 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Jay Chou"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    QVERIFY(linker.linkTrack(t2).ok());

    // Track 2 connects directly to 周杰伦
    const auto t2Artists = TestDbHelper::getTrackArtistIds(conn, t2);
    QCOMPARE(t2Artists.size(), 1);
    QCOMPARE(t2Artists.first(), jayId);

    // No Jay Chou artist entity was created
    QCOMPARE(TestDbHelper::getArtistId(conn, QStringLiteral("Jay Chou")), -1);
}

void TstArtistAliasCorrections::invalidProposalsRejectedAsWhole()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_invalid.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("周杰伦"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    const qint64 jayId = TestDbHelper::getArtistId(conn, QStringLiteral("周杰伦"));
    QVERIFY(jayId > 0);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    ArtistAliasProposal p1;
    p1.canonicalArtistId = jayId;
    p1.alias = QStringLiteral("Jay");
    p1.confidence = 0.8;

    ArtistAliasProposal p2;
    p2.canonicalArtistId = jayId;
    p2.alias = QStringLiteral("Jay Chou");
    p2.confidence = 1.5; // Invalid

    const auto addRes = store.addArtistAliasProposals(batchId, { p1, p2 });
    QVERIFY(!addRes.ok());
    QCOMPARE(addRes.error().code, QString(errc::kCorrectionInvalid));

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM corrections;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstArtistAliasCorrections::batchCountsIncludeAliasCorrections()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_counts.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("周杰伦"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    const qint64 jayId = TestDbHelper::getArtistId(conn, QStringLiteral("周杰伦"));
    QVERIFY(jayId > 0);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    ArtistAliasProposal p1;
    p1.canonicalArtistId = jayId;
    p1.alias = QStringLiteral("Jay");
    p1.confidence = 0.5;

    ArtistAliasProposal p2;
    p2.canonicalArtistId = jayId;
    p2.alias = QStringLiteral("Jay Chou");
    p2.confidence = 0.6;

    QVERIFY(store.addArtistAliasProposals(batchId, { p1, p2 }).ok());

    auto batches = store.batches().value();
    QCOMPARE(batches.size(), 1);
    QCOMPARE(batches.first().pending, 2);
    QCOMPARE(batches.first().accepted, 0);

    const auto rows = store.artistAliasCorrections(batchId).value();
    QCOMPARE(rows.size(), 2);
    QVERIFY(store.accept({ rows.first().id }).ok());

    batches = store.batches().value();
    QCOMPARE(batches.size(), 1);
    QCOMPARE(batches.first().pending, 1);
    QCOMPARE(batches.first().accepted, 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistAliasCorrections)

#include "tst_ArtistAliasCorrections.moc"
