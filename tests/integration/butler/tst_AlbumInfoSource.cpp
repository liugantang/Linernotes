// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/AlbumInfoSource.h>
#include <common/ManualClock.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::AlbumInfoSource;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionSource;
using linernotes::library::CorrectionStatus;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::library::TagField;
using linernotes::test::ManualClock;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertFile(
        const QSqlDatabase &db, qint64 rootId, const QString &path, qint64 durationMs = 180000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, duration_ms, "
                                 "first_seen_at, scanned_at) "
                                 "VALUES (?, ?, 1048576, 2000, ?, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(durationMs);
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

    static bool insertTrackIssue(const QSqlDatabase &db, qint64 trackId, const QString &kind,
        const QString &field, const QString &detail)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO track_issues (track_id, kind, field, detail, created_at) "
                           "VALUES (?, ?, ?, ?, 1000);"));
        q.addBindValue(trackId);
        q.addBindValue(kind);
        q.addBindValue(field);
        q.addBindValue(detail);
        return q.exec();
    }
};

class TstAlbumInfoSource : public QObject {
    Q_OBJECT

private slots:
    void pendingAlbumsFilteringAndMarkChecked();
    void loadAlbumInputStructure();
    void writeProposalsWithAutoAccept();
};

void TstAlbumInfoSource::pendingAlbumsFilteringAndMarkChecked()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_album_info_source.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // Album 1 (Incomplete: missing year, track_number, etc.)
    const qint64 f1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/Album1/01.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"), QStringLiteral("Album 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist 1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUMARTIST"), QStringLiteral("Artist 1"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    // Album 2 (Complete: all fields present)
    const qint64 f2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/Album2/01.mp3"));
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"), QStringLiteral("Album 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Artist 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUMARTIST"), QStringLiteral("Artist 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("DATE"), QStringLiteral("2020"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TRACKNUMBER"), QStringLiteral("1/1"));
    // 只缺碟总数：不入选
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("DISCNUMBER"), QStringLiteral("1"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    // Album 3 (Complete fields, but has needs_online title issue)
    const qint64 f3
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/Album3/01.mp3"));
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("DamagedTitle"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ALBUM"), QStringLiteral("Album 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ARTIST"), QStringLiteral("Artist 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("ALBUMARTIST"), QStringLiteral("Artist 3"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("DATE"), QStringLiteral("2021"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TRACKNUMBER"), QStringLiteral("1/1"));
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("DISCNUMBER"), QStringLiteral("1/1"));
    TestDbHelper::updateTagsReadAt(conn, t3);
    TestDbHelper::insertTrackIssue(conn, t3, QStringLiteral("needs_online"),
        QStringLiteral("title"), QStringLiteral("damaged"));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());
    QVERIFY(linker.linkTrack(t3).ok());

    // Query album IDs from database
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT id, title FROM albums ORDER BY id;")));
    QVERIFY(q.next());
    const qint64 album1Id = q.value(0).toLongLong();
    QVERIFY(q.next());
    const qint64 album2Id = q.value(0).toLongLong();
    QVERIFY(q.next());
    const qint64 album3Id = q.value(0).toLongLong();

    const AlbumInfoSource source(db);

    // 1. Pending check: album 1 and album 3 are pending; album 2 is not
    const auto pendingRes = source.pendingAlbums();
    QVERIFY(pendingRes.ok());
    const auto &pendingList = pendingRes.value();
    QCOMPARE(pendingList.size(), 2);
    QVERIFY(pendingList.contains(album1Id));
    QVERIFY(!pendingList.contains(album2Id));
    QVERIFY(pendingList.contains(album3Id));

    // 2. Mark checked for album 1 and 3
    const auto markRes
        = source.markChecked({ album1Id, album3Id }, QStringLiteral("gpt-4o"), 1, 5000);
    QVERIFY(markRes.ok());

    // 3. After marking checked, pending should be empty
    const auto pendingAfterRes = source.pendingAlbums();
    QVERIFY(pendingAfterRes.ok());
    QCOMPARE(pendingAfterRes.value().size(), 0);
}

void TstAlbumInfoSource::loadAlbumInputStructure()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_album_info_load.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // Album with 2 tracks across CD1 and CD2 directories
    const qint64 f2 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/MyAlbum/CD2/01 - Song2.flac"), 200000);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"), QStringLiteral("My Album"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("My Artist"));
    TestDbHelper::insertRawTag(
        conn, t2, QStringLiteral("ALBUMARTIST"), QStringLiteral("My Artist"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("DISCNUMBER"), QStringLiteral("2"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TRACKNUMBER"), QStringLiteral("1"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    const qint64 f1 = TestDbHelper::insertFile(
        conn, rootId, QStringLiteral("/music/MyAlbum/CD1/01 - Song1.flac"), 150000);
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("CorruptedTitle"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"), QStringLiteral("My Album"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("My Artist"));
    TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ALBUMARTIST"), QStringLiteral("My Artist"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("DISCNUMBER"), QStringLiteral("1"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TRACKNUMBER"), QStringLiteral("1"));
    TestDbHelper::updateTagsReadAt(conn, t1);
    TestDbHelper::insertTrackIssue(conn, t1, QStringLiteral("needs_online"),
        QStringLiteral("title"), QStringLiteral("damaged"));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT id FROM albums WHERE title = 'My Album';")) && q.next());
    const qint64 albumId = q.value(0).toLongLong();

    const AlbumInfoSource source(db);
    const auto loadRes = source.load({ albumId });
    QVERIFY(loadRes.ok());
    const auto &inputs = loadRes.value();
    QCOMPARE(inputs.size(), 1);

    const auto &albumInput = inputs.first();
    QCOMPARE(albumInput.albumId, albumId);
    QCOMPARE(albumInput.title, QStringLiteral("My Album"));
    QCOMPARE(albumInput.albumArtist, QStringLiteral("My Artist"));

    // Longest common parent directory
    QCOMPARE(albumInput.directory, QStringLiteral("/music/MyAlbum"));

    // Tracks sorted by disc (disc 1 before disc 2)
    QCOMPARE(albumInput.tracks.size(), 2);

    const auto &trk1 = albumInput.tracks.at(0);
    QCOMPARE(trk1.trackId, t1);
    QCOMPARE(trk1.relativePath, QStringLiteral("CD1/01 - Song1.flac"));
    QCOMPARE(trk1.durationMs, 150000);
    QCOMPARE(trk1.title, QStringLiteral("CorruptedTitle"));
    QCOMPARE(trk1.titleUnusable, true);
    QCOMPARE(trk1.discNumber.value_or(0), 1);
    QCOMPARE(trk1.trackNumber.value_or(0), 1);

    const auto &trk2 = albumInput.tracks.at(1);
    QCOMPARE(trk2.trackId, t2);
    QCOMPARE(trk2.relativePath, QStringLiteral("CD2/01 - Song2.flac"));
    QCOMPARE(trk2.durationMs, 200000);
    QCOMPARE(trk2.title, QStringLiteral("Song 2"));
    QCOMPARE(trk2.titleUnusable, false);
    QCOMPARE(trk2.discNumber.value_or(0), 2);
    QCOMPARE(trk2.trackNumber.value_or(0), 1);
}

void TstAlbumInfoSource::writeProposalsWithAutoAccept()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_album_info_write.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());

    const ManualClock clock(1000);
    CorrectionStore store(db, clock);

    const auto batchRes
        = store.createBatch(CorrectionKind::AlbumInfo, QStringLiteral("Test Batch"));
    QVERIFY(batchRes.ok());
    const qint64 batchId = batchRes.value();

    // 1. fromEvidence proposal with confidence 0.9, autoAccept threshold 0.5 -> should be Accepted
    const CorrectionProposal propEvidence {
        .trackId = t1,
        .field = TagField::TrackNumber,
        .oldValue = std::nullopt,
        .newValue = QStringLiteral("1"),
        .source = CorrectionSource::Llm,
        .confidence = 0.90,
        .reason = QStringLiteral("From file name"),
    };
    const auto addEvidenceRes = store.addProposals(batchId, { propEvidence }, 0.5);
    QVERIFY(addEvidenceRes.ok());

    // 2. fromKnowledge proposal with confidence 0.99, autoAccept threshold nullopt -> should remain
    // Pending
    const CorrectionProposal propKnowledge {
        .trackId = t1,
        .field = TagField::Year,
        .oldValue = std::nullopt,
        .newValue = QStringLiteral("2003"),
        .source = CorrectionSource::Llm,
        .confidence = 0.99,
        .reason = QStringLiteral("From model knowledge, unverified"),
    };
    const auto addKnowledgeRes = store.addProposals(batchId, { propKnowledge }, std::nullopt);
    QVERIFY(addKnowledgeRes.ok());

    // Verify statuses in store
    const auto corrListRes = store.corrections(batchId);
    QVERIFY(corrListRes.ok());
    const auto &corrections = corrListRes.value();
    QCOMPARE(corrections.size(), 2);

    for (const auto &row : corrections) {
        if (row.field == TagField::TrackNumber) {
            QCOMPARE(row.status, CorrectionStatus::Accepted);
        } else if (row.field == TagField::Year) {
            QCOMPARE(row.status, CorrectionStatus::Pending);
        }
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstAlbumInfoSource)

#include "tst_AlbumInfoSource.moc"
