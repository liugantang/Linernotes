// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Errors.h>
#include <library/LibraryQuery.h>
#include <library/LibrarySearch.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::CorrectionFilter;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionSource;
using linernotes::library::CorrectionStatus;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::LibraryQuery;
using linernotes::library::LibrarySearch;
using linernotes::library::Migrator;
using linernotes::library::TagField;
using linernotes::library::TrackFilter;
using linernotes::library::TrackRow;
using linernotes::library::TrackSortKey;
using linernotes::test::ManualClock;
namespace errc = linernotes::library::errc;

TrackRow onlyTrack(Database &db)
{
    const LibraryQuery query(db.connection().value());
    const auto res
        = query.tracks(TrackFilter { }, TrackSortKey::Default, Qt::AscendingOrder, 0, 10);
    if (!res.ok() || res.value().size() != 1) {
        return { };
    }
    return res.value().first();
}

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId,
        const QString &path = QStringLiteral("/music/song.mp3"))
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
};

class TstCorrectionStore : public QObject {
    Q_OBJECT

private slots:
    void addProposalsFillsOldValueAndStaysPending();
    void acceptUpdatesEffectiveAndRelinks();
    void autoAcceptAboveThreshold();
    void acceptEditedUsesUserValue();
    void rejectAndFilter();
    void revertBatchRestoresOriginal();
    void invalidConfidenceRejected();
    void deleteBatchWithVariousCorrections();
    void deleteEmptyBatchPhysicallyDeletes();
    void deleteBatchIfEmptyDeletesOnlyWhenEmpty();
    void deleteDecidedBatchesRemovesOnlyNonPending();
};

void TstCorrectionStore::addProposalsFillsOldValueAndStaysPending()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_proposals.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Original Title")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Artist A")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Album A")));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("DATE"), QStringLiteral("2020")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 1"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal proposal;
    proposal.trackId = trackId;
    proposal.field = TagField::Title;
    proposal.oldValue = std::nullopt;
    proposal.newValue = QStringLiteral("Proposed Title");
    proposal.source = CorrectionSource::Rule;
    proposal.confidence = 0.8;
    proposal.reason = QStringLiteral("Fix title");

    const auto addRes = store.addProposals(batchId, { proposal });
    QVERIFY(addRes.ok());

    const auto rowsRes = store.corrections(batchId);
    QVERIFY(rowsRes.ok());
    QCOMPARE(rowsRes.value().size(), 1);

    const auto &row = rowsRes.value().first();
    QCOMPARE(row.oldValue, QStringLiteral("Original Title"));
    QCOMPARE(row.newValue, QStringLiteral("Proposed Title"));
    QCOMPARE(row.status, CorrectionStatus::Pending);
    QCOMPARE(row.source, CorrectionSource::Rule);
    QCOMPARE(row.confidence, 0.8);
    QCOMPARE(row.reason, QStringLiteral("Fix title"));
    QCOMPARE(row.trackTitle, QStringLiteral("Original Title"));
    QCOMPARE(row.filePath, QStringLiteral("/music/song.mp3"));

    // Pending proposal does not change effective metadata
    QCOMPARE(onlyTrack(db).title, QStringLiteral("Original Title"));
}

void TstCorrectionStore::acceptUpdatesEffectiveAndRelinks()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_accept.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Old Artist")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Album 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists WHERE name = 'Old Artist'")));
    QVERIFY(q.next() && q.value(0).toInt() == 1);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::ArtistSplit, QStringLiteral("Split"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal proposal;
    proposal.trackId = trackId;
    proposal.field = TagField::Artist;
    proposal.newValue = QStringLiteral("New Artist");
    proposal.source = CorrectionSource::Rule;
    proposal.confidence = 0.8;

    QVERIFY(store.addProposals(batchId, { proposal }).ok());

    const auto rows = store.corrections(batchId).value();
    QCOMPARE(rows.size(), 1);
    const qint64 corrId = rows.first().id;

    clock.advance(100);
    const auto acceptRes = store.accept({ corrId });
    QVERIFY(acceptRes.ok());

    // Effective artist updated
    QCOMPARE(onlyTrack(db).artist, QStringLiteral("New Artist"));

    // Old artist cleaned up as orphan, New Artist created
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists WHERE name = 'Old Artist'")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM artists WHERE name = 'New Artist'")));
    QVERIFY(q.next() && q.value(0).toInt() == 1);

    // Search index finds track with New Artist
    LibrarySearch search(conn);
    const auto searchRes = search.search(QStringLiteral("New Artist"), 10);
    QVERIFY(searchRes.ok());
    QCOMPARE(searchRes.value().tracks.size(), 1);
    QCOMPARE(searchRes.value().tracks.first().trackId, trackId);
}

void TstCorrectionStore::autoAcceptAboveThreshold()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_auto_accept.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Artist 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::Mojibake, QStringLiteral("Mojibake"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal p1;
    p1.trackId = trackId;
    p1.field = TagField::Title;
    p1.newValue = QStringLiteral("Title Above");
    p1.confidence = 0.95;

    CorrectionProposal p2;
    p2.trackId = trackId;
    p2.field = TagField::Artist;
    p2.newValue = QStringLiteral("Artist Below");
    p2.confidence = 0.5;

    const auto addRes = store.addProposals(batchId, { p1, p2 }, 0.9);
    QVERIFY(addRes.ok());

    const auto rows = store.corrections(batchId).value();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).status, CorrectionStatus::Accepted);
    QCOMPARE(rows.at(1).status, CorrectionStatus::Pending);

    const auto track = onlyTrack(db);
    QCOMPARE(track.title, QStringLiteral("Title Above"));
    QCOMPARE(track.artist, QStringLiteral("Artist 1"));
}

void TstCorrectionStore::acceptEditedUsesUserValue()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_accept_edited.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::Manual, QStringLiteral("Edit Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal p;
    p.trackId = trackId;
    p.field = TagField::Title;
    p.newValue = QStringLiteral("Proposed Title");
    p.source = CorrectionSource::Rule;
    p.confidence = 0.8;

    QVERIFY(store.addProposals(batchId, { p }).ok());

    const auto rows = store.corrections(batchId).value();
    QCOMPARE(rows.size(), 1);
    const qint64 corrId = rows.first().id;

    clock.advance(100);
    const auto editRes = store.acceptEdited(corrId, QStringLiteral("User Edited Title"));
    QVERIFY(editRes.ok());

    const auto updatedRows = store.corrections(batchId).value();
    QCOMPARE(updatedRows.size(), 1);
    const auto &updatedRow = updatedRows.first();
    QCOMPARE(updatedRow.newValue, QStringLiteral("User Edited Title"));
    QCOMPARE(updatedRow.source, CorrectionSource::User);
    QCOMPARE(updatedRow.status, CorrectionStatus::Accepted);

    QCOMPARE(onlyTrack(db).title, QStringLiteral("User Edited Title"));
}

void TstCorrectionStore::rejectAndFilter()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_filter.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Orig Title")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Orig Artist")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Orig Album")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Filter Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal p1;
    p1.trackId = trackId;
    p1.field = TagField::Title;
    p1.newValue = QStringLiteral("T1");
    p1.confidence = 0.3;

    CorrectionProposal p2;
    p2.trackId = trackId;
    p2.field = TagField::Artist;
    p2.newValue = QStringLiteral("A1");
    p2.confidence = 0.8;

    CorrectionProposal p3;
    p3.trackId = trackId;
    p3.field = TagField::Album;
    p3.newValue = QStringLiteral("Alb1");
    p3.confidence = 0.95;

    QVERIFY(store.addProposals(batchId, { p1, p2, p3 }).ok());

    const auto allRows = store.corrections(batchId).value();
    QCOMPARE(allRows.size(), 3);
    const qint64 p1Id = allRows.at(0).id;
    const qint64 p2Id = allRows.at(1).id;

    // Reject p1
    QVERIFY(store.reject({ p1Id }).ok());
    QCOMPARE(store.corrections(batchId).value().at(0).status, CorrectionStatus::Rejected);

    // Filter by field
    CorrectionFilter fField;
    fField.field = TagField::Artist;
    const auto fieldRows = store.corrections(batchId, fField).value();
    QCOMPARE(fieldRows.size(), 1);
    QCOMPARE(fieldRows.first().id, p2Id);

    // Filter by status
    CorrectionFilter fStatus;
    fStatus.status = CorrectionStatus::Rejected;
    const auto statusRows = store.corrections(batchId, fStatus).value();
    QCOMPARE(statusRows.size(), 1);
    QCOMPARE(statusRows.first().id, p1Id);

    // Filter by confidence range
    CorrectionFilter fConf;
    fConf.minConfidence = 0.5;
    fConf.maxConfidence = 0.9;
    const auto confRows = store.corrections(batchId, fConf).value();
    QCOMPARE(confRows.size(), 1);
    QCOMPARE(confRows.first().id, p2Id);

    // Batches count check
    const auto batches = store.batches().value();
    QCOMPARE(batches.size(), 1);
    const auto &bInfo = batches.first();
    QCOMPARE(bInfo.pending, 2);
    QCOMPARE(bInfo.accepted, 0);
    QCOMPARE(bInfo.rejected, 1);
    QCOMPARE(bInfo.reverted, 0);
}

void TstCorrectionStore::revertBatchRestoresOriginal()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_revert.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Orig Title")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Orig Artist")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Orig Album")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Revert Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal p1;
    p1.trackId = trackId;
    p1.field = TagField::Title;
    p1.newValue = QStringLiteral("New Title");
    p1.confidence = 0.8;

    CorrectionProposal p2;
    p2.trackId = trackId;
    p2.field = TagField::Artist;
    p2.newValue = QStringLiteral("New Artist");
    p2.confidence = 0.8;

    CorrectionProposal p3;
    p3.trackId = trackId;
    p3.field = TagField::Album;
    p3.newValue = QStringLiteral("New Album");
    p3.confidence = 0.8;

    QVERIFY(store.addProposals(batchId, { p1, p2, p3 }).ok());

    const auto rows = store.corrections(batchId).value();
    QCOMPARE(rows.size(), 3);

    // Accept p1 and p2, leave p3 pending
    QVERIFY(store.accept({ rows.at(0).id, rows.at(1).id }).ok());

    const auto tModified = onlyTrack(db);
    QCOMPARE(tModified.title, QStringLiteral("New Title"));
    QCOMPARE(tModified.artist, QStringLiteral("New Artist"));

    // Revert batch
    clock.advance(200);
    QVERIFY(store.revertBatch(batchId).ok());

    // Effective metadata restored to originals
    const auto tRestored = onlyTrack(db);
    QCOMPARE(tRestored.title, QStringLiteral("Orig Title"));
    QCOMPARE(tRestored.artist, QStringLiteral("Orig Artist"));

    // Check batch info: reverted 2, rejected 1
    const auto batches = store.batches().value();
    QCOMPARE(batches.size(), 1);
    const auto &bInfo = batches.first();
    QCOMPARE(bInfo.reverted, 2);
    QCOMPARE(bInfo.rejected, 1);
    QCOMPARE(bInfo.accepted, 0);
    QCOMPARE(bInfo.pending, 0);
    QCOMPARE(bInfo.revertedAt, std::optional<qint64>(1200));

    // Reverting again is a no-op
    QVERIFY(store.revertBatch(batchId).ok());
    const auto batches2 = store.batches().value();
    QCOMPARE(batches2.first().reverted, 2);
    QCOMPARE(batches2.first().rejected, 1);
}

void TstCorrectionStore::invalidConfidenceRejected()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_invalid_conf.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal p;
    p.trackId = trackId;
    p.field = TagField::Title;
    p.newValue = QStringLiteral("Bad");
    p.confidence = 1.5;

    const auto addRes = store.addProposals(batchId, { p });
    QVERIFY(!addRes.ok());
    QCOMPARE(addRes.error().code, QString(errc::kCorrectionInvalid));

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM corrections;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

void TstCorrectionStore::deleteBatchWithVariousCorrections()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_delete_batch.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Orig Title")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Orig Artist")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Orig Album")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 1"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    CorrectionProposal p1;
    p1.trackId = trackId;
    p1.field = TagField::Title;
    p1.newValue = QStringLiteral("New Title");
    p1.confidence = 0.9;
    p1.reason = QStringLiteral("Fix title");

    CorrectionProposal p2;
    p2.trackId = trackId;
    p2.field = TagField::Artist;
    p2.newValue = QStringLiteral("New Artist");
    p2.confidence = 0.8;
    p2.reason = QStringLiteral("Fix artist");

    CorrectionProposal p3;
    p3.trackId = trackId;
    p3.field = TagField::Album;
    p3.newValue = QStringLiteral("New Album");
    p3.confidence = 0.7;
    p3.reason = QStringLiteral("Fix album");

    QVERIFY(store.addProposals(batchId, { p1, p2, p3 }).ok());

    const auto rows = store.corrections(batchId).value();
    QCOMPARE(rows.size(), 3);

    // Accept p1 (title)
    QVERIFY(store.accept({ rows.at(0).id }).ok());
    // Reject p2 (artist)
    QVERIFY(store.reject({ rows.at(1).id }).ok());
    // p3 (album) remains pending

    QCOMPARE(onlyTrack(db).title, QStringLiteral("New Title"));

    // Delete batch
    clock.advance(100);
    const auto delRes = store.deleteBatch(batchId);
    QVERIFY(delRes.ok());

    // batches() no longer contains the batch
    const auto batches = store.batches().value();
    QCOMPARE(batches.size(), 0);

    // accepted change still in effect
    QCOMPARE(onlyTrack(db).title, QStringLiteral("New Title"));

    // Check database rows directly:
    // accepted row preserved
    QSqlQuery qAcc(conn);
    qAcc.prepare(QStringLiteral("SELECT status FROM corrections WHERE id = ?;"));
    qAcc.addBindValue(rows.at(0).id);
    QVERIFY(qAcc.exec() && qAcc.next());
    QCOMPARE(qAcc.value(0).toString(), QStringLiteral("accepted"));

    // rejected row preserved
    QSqlQuery qRej(conn);
    qRej.prepare(QStringLiteral("SELECT status FROM corrections WHERE id = ?;"));
    qRej.addBindValue(rows.at(1).id);
    QVERIFY(qRej.exec() && qRej.next());
    QCOMPARE(qRej.value(0).toString(), QStringLiteral("rejected"));

    // pending row deleted
    QSqlQuery qPen(conn);
    qPen.prepare(QStringLiteral("SELECT COUNT(*) FROM corrections WHERE id = ?;"));
    qPen.addBindValue(rows.at(2).id);
    QVERIFY(qPen.exec() && qPen.next());
    QCOMPARE(qPen.value(0).toInt(), 0);

    // correction_batches soft deleted
    QSqlQuery qBatch(conn);
    qBatch.prepare(QStringLiteral("SELECT deleted_at FROM correction_batches WHERE id = ?;"));
    qBatch.addBindValue(batchId);
    QVERIFY(qBatch.exec() && qBatch.next());
    QCOMPARE(qBatch.value(0).toLongLong(), 1100LL);

    // Deleting again returns kCorrectionNotFound
    const auto delAgain = store.deleteBatch(batchId);
    QVERIFY(!delAgain.ok());
    QCOMPARE(delAgain.error().code, QString(errc::kCorrectionNotFound));
}

void TstCorrectionStore::deleteEmptyBatchPhysicallyDeletes()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_empty_batch.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchIdRes
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Empty Batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    QCOMPARE(store.batches().value().size(), 1);

    const auto delRes = store.deleteBatch(batchId);
    QVERIFY(delRes.ok());

    QCOMPARE(store.batches().value().size(), 0);

    // Physically deleted from correction_batches
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM correction_batches WHERE id = ?;"));
    q.addBindValue(batchId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);

    // Deleting again returns kCorrectionNotFound
    const auto delAgain = store.deleteBatch(batchId);
    QVERIFY(!delAgain.ok());
    QCOMPARE(delAgain.error().code, QString(errc::kCorrectionNotFound));
}

void TstCorrectionStore::deleteBatchIfEmptyDeletesOnlyWhenEmpty()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_delete_if_empty.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    // Empty batch -> physically deleted
    const auto b1
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Empty Batch")).value();
    QVERIFY(store.deleteBatchIfEmpty(b1).ok());

    QSqlQuery q1(conn);
    q1.prepare(QStringLiteral("SELECT COUNT(*) FROM correction_batches WHERE id = ?;"));
    q1.addBindValue(b1);
    QVERIFY(q1.exec() && q1.next());
    QCOMPARE(q1.value(0).toInt(), 0);

    // Non-empty batch -> no-op
    const auto b2
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Non-empty Batch")).value();
    CorrectionProposal p;
    p.trackId = trackId;
    p.field = TagField::Title;
    p.newValue = QStringLiteral("Title");
    p.confidence = 0.9;
    QVERIFY(store.addProposals(b2, { p }).ok());

    QVERIFY(store.deleteBatchIfEmpty(b2).ok());

    QSqlQuery q2(conn);
    q2.prepare(QStringLiteral("SELECT COUNT(*), deleted_at FROM correction_batches WHERE id = ?;"));
    q2.addBindValue(b2);
    QVERIFY(q2.exec() && q2.next());
    QCOMPARE(q2.value(0).toInt(), 1);
    QVERIFY(q2.value(1).isNull());
}

void TstCorrectionStore::deleteDecidedBatchesRemovesOnlyNonPending()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_delete_decided.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    ManualClock clock(1000);
    CorrectionStore store(db, clock);

    // Batch 1: empty
    const auto b1 = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 1")).value();

    // Batch 2: all accepted
    const auto b2 = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 2")).value();
    CorrectionProposal p2;
    p2.trackId = trackId;
    p2.field = TagField::Title;
    p2.newValue = QStringLiteral("Title 2");
    p2.confidence = 0.9;
    QVERIFY(store.addProposals(b2, { p2 }).ok());
    const auto rows2 = store.corrections(b2).value();
    QVERIFY(store.accept({ rows2.at(0).id }).ok());

    // Batch 3: all rejected
    const auto b3 = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 3")).value();
    CorrectionProposal p3;
    p3.trackId = trackId;
    p3.field = TagField::Title;
    p3.newValue = QStringLiteral("Title 3");
    p3.confidence = 0.8;
    QVERIFY(store.addProposals(b3, { p3 }).ok());
    const auto rows3 = store.corrections(b3).value();
    QVERIFY(store.reject({ rows3.at(0).id }).ok());

    // Batch 4: has pending
    const auto b4 = store.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 4")).value();
    CorrectionProposal p4a;
    p4a.trackId = trackId;
    p4a.field = TagField::Title;
    p4a.newValue = QStringLiteral("Title 4a");
    p4a.confidence = 0.9;

    CorrectionProposal p4b;
    p4b.trackId = trackId;
    p4b.field = TagField::Artist;
    p4b.newValue = QStringLiteral("Artist 4b");
    p4b.confidence = 0.7;

    QVERIFY(store.addProposals(b4, { p4a, p4b }).ok());
    const auto rows4 = store.corrections(b4).value();
    QVERIFY(store.accept({ rows4.at(0).id }).ok());
    // rows4[1] is pending

    QCOMPARE(store.batches().value().size(), 4);

    // deleteDecidedBatches should delete b1 (empty), b2 (accepted), b3 (rejected) = 3 batches
    const auto delCount = store.deleteDecidedBatches();
    QVERIFY(delCount.ok());
    QCOMPARE(delCount.value(), 3);

    // Only b4 remains in batches()
    const auto remaining = store.batches().value();
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().id, b4);

    // b1 is physically deleted
    QSqlQuery q1(conn);
    q1.prepare(QStringLiteral("SELECT COUNT(*) FROM correction_batches WHERE id = ?;"));
    q1.addBindValue(b1);
    QVERIFY(q1.exec() && q1.next());
    QCOMPARE(q1.value(0).toInt(), 0);

    // b2 and b3 are soft deleted
    QSqlQuery q2(conn);
    q2.prepare(QStringLiteral("SELECT deleted_at FROM correction_batches WHERE id = ?;"));
    q2.addBindValue(b2);
    QVERIFY(q2.exec() && q2.next());
    QVERIFY(!q2.value(0).isNull());

    QSqlQuery q3(conn);
    q3.prepare(QStringLiteral("SELECT deleted_at FROM correction_batches WHERE id = ?;"));
    q3.addBindValue(b3);
    QVERIFY(q3.exec() && q3.next());
    QVERIFY(!q3.value(0).isNull());

    // b4 is not deleted
    QSqlQuery q4(conn);
    q4.prepare(QStringLiteral("SELECT deleted_at FROM correction_batches WHERE id = ?;"));
    q4.addBindValue(b4);
    QVERIFY(q4.exec() && q4.next());
    QVERIFY(q4.value(0).isNull());
}

} // namespace

QTEST_GUILESS_MAIN(TstCorrectionStore)

#include "tst_CorrectionStore.moc"
