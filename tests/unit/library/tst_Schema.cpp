// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId,
        const QString &path = QStringLiteral("/music/song.mp3"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        q.addBindValue(1024 * 1024);
        q.addBindValue(2000);
        q.addBindValue(2000);
        q.addBindValue(2000);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId,
        const QVariant &cueIndex = QVariant(), const QVariant &albumId = QVariant(),
        qint64 tagsReadAt = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, ?, ?, ?, ?);"));
        q.addBindValue(fileId);
        q.addBindValue(cueIndex.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : cueIndex);
        q.addBindValue(albumId.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : albumId);
        q.addBindValue(tagsReadAt);
        q.addBindValue(3000);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static bool insertRawTag(const QSqlDatabase &db, qint64 trackId, const QString &tagType,
        int priority, const QString &key, int ordinal, const QString &value)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
            "VALUES (?, ?, ?, ?, ?, ?);"));
        q.addBindValue(trackId);
        q.addBindValue(tagType);
        q.addBindValue(priority);
        q.addBindValue(key);
        q.addBindValue(ordinal);
        q.addBindValue(value);
        return q.exec();
    }

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 timestamp)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(timestamp);
        q.addBindValue(trackId);
        return q.exec();
    }
};

class TstSchema : public QObject {
    Q_OBJECT

private slots:
    void migratesFreshDatabaseToLatest();
    void stepwiseMigrationEqualsFresh();
    void strictTablesRejectWrongTypes();
    void checkConstraints_data();
    void checkConstraints();
    void cascadeDeletes();
    void oneTrackPerNonCueFile();
    void effectiveUsesRawTags();
    void effectivePrefersLowestPriorityTagType();
    void effectiveLayering();
    void effectiveParsesIntegers_data();
    void effectiveParsesIntegers();
    void rawTagInsertsDoNotRefreshUntilTagsReadAtUpdated();
    void refreshTouchesOnlyAffectedTrack();
    void refreshQueryPlanUsesIndexes();
    void refreshScalesToLargeLibrary();
};

void TstSchema::migratesFreshDatabaseToLatest()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("fresh.db")));
    const Migrator migrator;
    const auto openRes = db.open(migrator);
    QVERIFY(openRes.ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const auto verRes = Migrator::currentVersion(conn);
    QVERIFY(verRes.ok());
    QCOMPARE(verRes.value(), 3);

    // Verify all required tables exist in sqlite_schema
    const QStringList expectedTables = {
        QStringLiteral("library_roots"),
        QStringLiteral("files"),
        QStringLiteral("covers"),
        QStringLiteral("albums"),
        QStringLiteral("works"),
        QStringLiteral("tracks"),
        QStringLiteral("raw_tags"),
        QStringLiteral("artists"),
        QStringLiteral("artist_aliases"),
        QStringLiteral("track_artists"),
        QStringLiteral("album_artists"),
        QStringLiteral("playlists"),
        QStringLiteral("playlist_items"),
        QStringLiteral("favorites"),
        QStringLiteral("ratings"),
        QStringLiteral("correction_batches"),
        QStringLiteral("corrections"),
        QStringLiteral("user_overrides"),
        QStringLiteral("effective_metadata"),
        QStringLiteral("play_events"),
        QStringLiteral("moments"),
        QStringLiteral("audio_features"),
        QStringLiteral("embeddings"),
        QStringLiteral("llm_cache"),
        QStringLiteral("change_log"),
        QStringLiteral("schema_version"),
    };

    QSet<QString> actualTables;
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT name FROM sqlite_schema WHERE type='table';")));
    while (q.next()) {
        actualTables.insert(q.value(0).toString());
    }

    for (const QString &tbl : expectedTables) {
        QVERIFY2(
            actualTables.contains(tbl), qPrintable(QStringLiteral("Missing table: %1").arg(tbl)));
    }

    // Verify effective_metadata_view exists
    QVERIFY(q.exec(QStringLiteral(
        "SELECT name FROM sqlite_schema WHERE type='view' AND name='effective_metadata_view';")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("effective_metadata_view"));

    // PRAGMA foreign_key_check must return 0 rows
    QVERIFY(q.exec(QStringLiteral("PRAGMA foreign_key_check;")));
    QVERIFY(!q.next());

    // PRAGMA integrity_check must be 'ok'
    QVERIFY(q.exec(QStringLiteral("PRAGMA integrity_check;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("ok"));
}

void TstSchema::stepwiseMigrationEqualsFresh()
{
    const QTemporaryDir stepMigDir;
    QVERIFY(stepMigDir.isValid());
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    // Copy only 0001_core_schema.sql to stepMigDir
    QFile file0001(QStringLiteral(":/migrations/0001_core_schema.sql"));
    QVERIFY(file0001.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray sql0001 = file0001.readAll();
    file0001.close();

    QFile dest0001(QDir(stepMigDir.path()).filePath(QStringLiteral("0001_core_schema.sql")));
    QVERIFY(dest0001.open(QIODevice::WriteOnly | QIODevice::Text));
    dest0001.write(sql0001);
    dest0001.close();

    const QString dbPath = dbDir.filePath(QStringLiteral("stepwise.db"));
    Database db(dbPath);
    const Migrator stepMigrator(stepMigDir.path());
    const auto openRes1 = db.open(stepMigrator);
    QVERIFY(openRes1.ok());

    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QCOMPARE(Migrator::currentVersion(conn).value(), 1);

    // Insert root, file, track, raw_tags
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    QVERIFY(rootId > 0);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    QVERIFY(fileId > 0);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, QVariant(), QVariant(), 100);
    QVERIFY(trackId > 0);

    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Step Title")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Step Artist")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("DATE"), 0, QStringLiteral("2021-05-12")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TRACKNUMBER"), 0, QStringLiteral("4/10")));

    // Upgrade to latest using default Migrator (which includes 0001, 0002, 0003)
    const Migrator fullMigrator;
    const auto migRes = fullMigrator.migrate(conn);
    QVERIFY(migRes.ok());
    QCOMPARE(Migrator::currentVersion(conn).value(), 3);

    // Verify 0002 initial population filled effective_metadata for pre-existing track
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT title, artist, year, track_number, track_total FROM "
                             "effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Step Title"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("Step Artist"));
    QCOMPARE(q.value(2).toInt(), 2021);
    QCOMPARE(q.value(3).toInt(), 4);
    QCOMPARE(q.value(4).toInt(), 10);
}

void TstSchema::strictTablesRejectWrongTypes()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("strict.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    // Writing string 'abc' to INTEGER column added_at in library_roots must fail in STRICT table
    QVERIFY(!q.exec(
        QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES ('/music', 'abc');")));

    // Writing string 'abc' to INTEGER column size in files must fail
    QVERIFY(!q.exec(
        QStringLiteral("INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
                       "VALUES (1, '/p', 'abc', 1, 1, 1);")));

    // Writing string 'abc' to INTEGER column rating in ratings must fail
    QVERIFY(!q.exec(QStringLiteral(
        "INSERT INTO ratings (track_id, rating, updated_at) VALUES (1, 'abc', 1);")));
}

void TstSchema::checkConstraints_data()
{
    QTest::addColumn<QString>("sql");

    QTest::newRow("invalid-tag-type") << QStringLiteral(
        "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
        "VALUES (1, 'invalid_tag_type', 0, 'TITLE', 0, 'Song');");

    QTest::newRow("invalid-artist-role")
        << QStringLiteral("INSERT INTO track_artists (track_id, artist_id, role) "
                          "VALUES (1, 1, 'guitarist');");

    QTest::newRow("invalid-correction-status")
        << QStringLiteral("INSERT INTO corrections (entity_type, entity_id, field, source, "
                          "confidence, status, created_at) "
                          "VALUES ('track', 1, 'title', 'rule', 0.8, 'invalid_status', 100);");

    QTest::newRow("confidence-greater-than-one")
        << QStringLiteral("INSERT INTO corrections (entity_type, entity_id, field, source, "
                          "confidence, status, created_at) "
                          "VALUES ('track', 1, 'title', 'rule', 1.5, 'pending', 100);");

    QTest::newRow("confidence-less-than-zero")
        << QStringLiteral("INSERT INTO corrections (entity_type, entity_id, field, source, "
                          "confidence, status, created_at) "
                          "VALUES ('track', 1, 'title', 'rule', -0.1, 'pending', 100);");

    QTest::newRow("rating-greater-than-5")
        << QStringLiteral("INSERT INTO ratings (track_id, rating, updated_at) "
                          "VALUES (1, 6, 100);");

    QTest::newRow("rating-less-than-1")
        << QStringLiteral("INSERT INTO ratings (track_id, rating, updated_at) "
                          "VALUES (1, 0, 100);");

    QTest::newRow("smart-playlist-null-rule")
        << QStringLiteral("INSERT INTO playlists (name, kind, rule, created_at, updated_at) "
                          "VALUES ('Smart PL', 'smart', NULL, 100, 100);");

    QTest::newRow("invalid-field-name-in-user-overrides") << QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'invalid_field', 'Val', 100, 100);");

    QTest::newRow("invalid-field-name-in-corrections")
        << QStringLiteral("INSERT INTO corrections (entity_type, entity_id, field, source, "
                          "confidence, status, created_at) "
                          "VALUES ('track', 1, 'invalid_field', 'rule', 0.8, 'pending', 100);");
}

void TstSchema::checkConstraints()
{
    QFETCH(QString, sql);

    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("chk.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    // Setup valid prerequisite rows so foreign keys do not trigger FK failure instead of CHECK
    // failure
    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    TestDbHelper::insertTrack(conn, fileId);

    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO artists (id, name, created_at) VALUES (1, 'Artist 1', 100);")));

    // Now execute invalid statement, it MUST fail due to CHECK constraint
    QVERIFY(!q.exec(sql));
}

void TstSchema::cascadeDeletes()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("cascade.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);

    // Insert album
    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO albums (id, title, created_at) VALUES (10, 'Album X', 100);")));

    // Insert artist
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO artists (id, name, created_at) VALUES (20, 'Artist Y', 100);")));

    // Insert track associated with fileId and album 10
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, QVariant(), 10, 100);
    QVERIFY(trackId > 0);

    // Insert raw_tag and update tags_read_at
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Cascade Title")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 200));

    // Verify effective_metadata exists
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM effective_metadata WHERE track_id = 1;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 1);

    // Insert track_artists
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO track_artists (track_id, artist_id, role) VALUES (1, 20, 'artist');")));

    // Insert playlist and playlist_items
    QVERIFY(q.exec(QStringLiteral("INSERT INTO playlists (id, name, kind, created_at, updated_at) "
                                  "VALUES (30, 'P1', 'manual', 100, 100);")));
    QVERIFY(q.exec(QStringLiteral("INSERT INTO playlist_items (playlist_id, track_id, position, "
                                  "added_at) VALUES (30, 1, 0, 100);")));

    // Insert ratings
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO ratings (track_id, rating, updated_at) VALUES (1, 5, 100);")));

    // Insert favorites for track, album, artist
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('track', 1, 100);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('album', 10, 100);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('artist', 20, 100);")));

    // Insert play_events
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO play_events (id, track_id, started_at) VALUES (40, 1, 100);")));

    // Delete file
    QVERIFY(q.exec(QStringLiteral("DELETE FROM files WHERE id = 1;")));

    // Check tracks, raw_tags, effective_metadata, track_artists, playlist_items, ratings deleted
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM raw_tags WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM effective_metadata WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM ratings WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    // favorites for track 1 deleted by trigger
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM favorites WHERE entity_type = 'track' AND entity_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    // play_events row retained, but track_id set to NULL
    QVERIFY(q.exec(QStringLiteral("SELECT id, track_id FROM play_events WHERE id = 40;")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 40);
    QVERIFY(q.value(1).isNull());

    // Insert a second file and track associated with album 10 to test album deletion
    const qint64 fileId2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song2.mp3"));
    const qint64 trackId2 = TestDbHelper::insertTrack(conn, fileId2, QVariant(), 10, 100);
    QVERIFY(trackId2 > 0);

    // Delete album 10
    QVERIFY(q.exec(QStringLiteral("DELETE FROM albums WHERE id = 10;")));

    // favorites for album 10 deleted by trigger
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM favorites WHERE entity_type = 'album' AND entity_id = 10;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    // tracks.album_id set to NULL
    q.prepare(QStringLiteral("SELECT album_id FROM tracks WHERE id = ?;"));
    q.addBindValue(trackId2);
    QVERIFY(q.exec() && q.next());
    QVERIFY(q.value(0).isNull());
}

void TstSchema::oneTrackPerNonCueFile()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("cue.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);

    // First track without cue_index -> success
    const qint64 t1 = TestDbHelper::insertTrack(conn, fileId, QVariant(), QVariant(), 0);
    QVERIFY(t1 > 0);

    // Second track for same file without cue_index -> fails due to partial unique index
    const qint64 t2 = TestDbHelper::insertTrack(conn, fileId, QVariant(), QVariant(), 0);
    QCOMPARE(t2, -1);

    // Cue tracks for same file with distinct cue_indexes -> success
    const qint64 tCue1 = TestDbHelper::insertTrack(conn, fileId, 1, QVariant(), 0);
    QVERIFY(tCue1 > 0);
    const qint64 tCue2 = TestDbHelper::insertTrack(conn, fileId, 2, QVariant(), 0);
    QVERIFY(tCue2 > 0);

    // Duplicate cue_index for same file -> fails
    const qint64 tCue1Dup = TestDbHelper::insertTrack(conn, fileId, 1, QVariant(), 0);
    QCOMPARE(tCue1Dup, -1);
}

void TstSchema::effectiveUsesRawTags()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("raw_tags.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Song Title")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("A")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 1, QStringLiteral("B")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("DATE"), 0, QStringLiteral("2003-07-31")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TRACKNUMBER"), 0, QStringLiteral("3/12")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("DISCNUMBER"), 0, QStringLiteral("1")));

    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 2000));

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT title, artist, year, track_number, track_total, disc_number, disc_total "
        "FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());

    QCOMPARE(q.value(0).toString(), QStringLiteral("Song Title"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("A / B"));
    QCOMPARE(q.value(2).toInt(), 2003);
    QCOMPARE(q.value(3).toInt(), 3);
    QCOMPARE(q.value(4).toInt(), 12);
    QCOMPARE(q.value(5).toInt(), 1);
    QVERIFY(q.value(6).isNull());
}

void TstSchema::effectivePrefersLowestPriorityTagType()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("priority.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    // ARTIST: id3v2 priority 0 (multi-val) vs id3v1 priority 9
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Artist2_A")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 1, QStringLiteral("Artist2_B")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v1"), 9,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Artist1")));

    // GENRE: only id3v1 priority 9
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v1"), 9,
        QStringLiteral("GENRE"), 0, QStringLiteral("Rock")));

    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 2000));

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT artist, genre FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());

    QCOMPARE(q.value(0).toString(), QStringLiteral("Artist2_A / Artist2_B"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("Rock"));
}

void TstSchema::effectiveLayering()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("layering.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    auto getEffectiveTitle = [&]() -> QVariant {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
        q.addBindValue(trackId);
        if (q.exec() && q.next()) {
            return q.value(0);
        }
        return { };
    };

    // 1. Raw layer
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Raw Title")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 100));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Raw Title"));

    // 2. Add pending correction -> no change
    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO corrections (id, entity_type, entity_id, field, new_value, "
                       "source, confidence, status, created_at) "
                       "VALUES (1, 'track', 1, 'title', 'Corr 1', 'rule', 0.9, 'pending', 200);")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Raw Title"));

    // 3. Mark accepted -> adopt correction
    QVERIFY(q.exec(QStringLiteral(
        "UPDATE corrections SET status = 'accepted', decided_at = 300 WHERE id = 1;")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Corr 1"));

    // 4. Add later accepted correction -> adopt new value
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO corrections (id, entity_type, entity_id, field, new_value, source, "
        "confidence, status, created_at, decided_at) "
        "VALUES (2, 'track', 1, 'title', 'Corr 2', 'llm', 0.95, 'accepted', 400, 400);")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Corr 2"));

    // 5. Add user override -> adopt override value
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'title', 'Override Title', 500, 500);")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Override Title"));

    // 6. User override value is NULL -> effective field is NULL
    QVERIFY(q.exec(QStringLiteral("UPDATE user_overrides SET value = NULL, updated_at = 600 WHERE "
                                  "track_id = 1 AND field = 'title';")));
    QVERIFY(getEffectiveTitle().isNull());

    // 7. Delete override -> reverts to latest accepted correction (Corr 2)
    QVERIFY(q.exec(
        QStringLiteral("DELETE FROM user_overrides WHERE track_id = 1 AND field = 'title';")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Corr 2"));

    // 8. Revert correction 2 -> reverts to Corr 1
    QVERIFY(q.exec(QStringLiteral("UPDATE corrections SET status = 'reverted' WHERE id = 2;")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Corr 1"));

    // 9. Revert correction 1 -> reverts to raw title
    QVERIFY(q.exec(QStringLiteral("UPDATE corrections SET status = 'reverted' WHERE id = 1;")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Raw Title"));
}

void TstSchema::effectiveParsesIntegers_data()
{
    QTest::addColumn<QString>("dateStr");
    QTest::addColumn<QVariant>("expectedYear");
    QTest::addColumn<QString>("trackNumStr");
    QTest::addColumn<QVariant>("expectedTrackNum");
    QTest::addColumn<QString>("trackTotStr");
    QTest::addColumn<QVariant>("expectedTrackTot");

    QTest::newRow("date-1999") << QStringLiteral("1999") << QVariant(1999) << QStringLiteral("1")
                               << QVariant(1) << QString() << QVariant();

    QTest::newRow("date-iso-1999-01-02")
        << QStringLiteral("1999-01-02") << QVariant(1999) << QStringLiteral("1") << QVariant(1)
        << QString() << QVariant();

    QTest::newRow("date-99") << QStringLiteral("99") << QVariant() << QStringLiteral("1")
                             << QVariant(1) << QString() << QVariant();

    QTest::newRow("date-abcd") << QStringLiteral("abcd") << QVariant() << QStringLiteral("1")
                               << QVariant(1) << QString() << QVariant();

    QTest::newRow("date-empty") << QStringLiteral("") << QVariant() << QStringLiteral("1")
                                << QVariant(1) << QString() << QVariant();

    QTest::newRow("track-07") << QString() << QVariant() << QStringLiteral("07") << QVariant(7)
                              << QString() << QVariant();

    QTest::newRow("track-7/10") << QString() << QVariant() << QStringLiteral("7/10") << QVariant(7)
                                << QString() << QVariant(10);

    QTest::newRow("track-x") << QString() << QVariant() << QStringLiteral("x") << QVariant()
                             << QString() << QVariant();

    QTest::newRow("track-/10") << QString() << QVariant() << QStringLiteral("/10") << QVariant()
                               << QString() << QVariant(10);

    QTest::newRow("tracktotal-prefers-tracktotal-tag")
        << QString() << QVariant() << QStringLiteral("7/10") << QVariant(7) << QStringLiteral("12")
        << QVariant(12);
}

void TstSchema::effectiveParsesIntegers()
{
    QFETCH(QString, dateStr);
    QFETCH(QVariant, expectedYear);
    QFETCH(QString, trackNumStr);
    QFETCH(QVariant, expectedTrackNum);
    QFETCH(QString, trackTotStr);
    QFETCH(QVariant, expectedTrackTot);

    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("integers.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    if (!dateStr.isNull()) {
        QVERIFY(TestDbHelper::insertRawTag(
            conn, trackId, QStringLiteral("id3v2"), 0, QStringLiteral("DATE"), 0, dateStr));
    }
    if (!trackNumStr.isNull()) {
        QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
            QStringLiteral("TRACKNUMBER"), 0, trackNumStr));
    }
    if (!trackTotStr.isNull()) {
        QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
            QStringLiteral("TRACKTOTAL"), 0, trackTotStr));
    }

    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 2000));

    QSqlQuery q(conn);
    q.prepare(QStringLiteral(
        "SELECT year, track_number, track_total FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());

    if (expectedYear.isNull()) {
        QVERIFY(q.value(0).isNull());
    } else {
        QCOMPARE(q.value(0).toInt(), expectedYear.toInt());
    }

    if (expectedTrackNum.isNull()) {
        QVERIFY(q.value(1).isNull());
    } else {
        QCOMPARE(q.value(1).toInt(), expectedTrackNum.toInt());
    }

    if (expectedTrackTot.isNull()) {
        QVERIFY(q.value(2).isNull());
    } else {
        QCOMPARE(q.value(2).toInt(), expectedTrackTot.toInt());
    }
}

void TstSchema::rawTagInsertsDoNotRefreshUntilTagsReadAtUpdated()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("norefresh.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);

    // Insert track initially with tags_read_at = 100
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, QVariant(), QVariant(), 100);
    QVERIFY(trackId > 0);

    // Initial effective_metadata title is NULL
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());
    QVERIFY(q.value(0).isNull());

    // Insert raw_tag without updating tags_read_at
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Lazy Refresh Title")));

    // effective_metadata title MUST STILL BE NULL (no raw_tags trigger)
    q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());
    QVERIFY(q.value(0).isNull());

    // Now update tags_read_at
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 200));

    // effective_metadata title is now updated
    q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Lazy Refresh Title"));
}

void TstSchema::refreshTouchesOnlyAffectedTrack()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("touch.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 fileId2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song2.mp3"));

    const qint64 t1 = TestDbHelper::insertTrack(conn, fileId1, QVariant(), QVariant(), 100);
    const qint64 t2 = TestDbHelper::insertTrack(conn, fileId2, QVariant(), QVariant(), 100);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("id3v2"), 0, QStringLiteral("TITLE"), 0, QStringLiteral("T1")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, t2, QStringLiteral("id3v2"), 0, QStringLiteral("TITLE"), 0, QStringLiteral("T2")));

    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1, 200));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2, 200));

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT updated_at FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(t2);
    QVERIFY(q.exec() && q.next());
    const qint64 t2InitialUpdated = q.value(0).toLongLong();

    // Sleep briefly so unixepoch subsec changes
    QThread::msleep(10);

    // Modify user_override for track 1 only
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'title', 'New T1', 300, 300);")));

    // Check track 2 updated_at is completely untouched
    q.prepare(QStringLiteral("SELECT updated_at FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(t2);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toLongLong(), t2InitialUpdated);
}

void TstSchema::refreshQueryPlanUsesIndexes()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("qplan.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "EXPLAIN QUERY PLAN SELECT * FROM effective_metadata_view WHERE track_id = 1;")));

    QStringList planDetails;
    while (q.next()) {
        planDetails.append(q.value(3).toString());
    }
    const QString fullPlan = planDetails.join(QLatin1Char('\n'));

    QVERIFY2(!fullPlan.contains(QStringLiteral("SCAN raw_tags")), qPrintable(fullPlan));
    QVERIFY2(!fullPlan.contains(QStringLiteral("SCAN corrections")), qPrintable(fullPlan));
    QVERIFY2(!fullPlan.contains(QStringLiteral("SCAN user_overrides")), qPrintable(fullPlan));
    QVERIFY2(!fullPlan.contains(QStringLiteral("SCAN tracks")), qPrintable(fullPlan));
}

void TstSchema::refreshScalesToLargeLibrary()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("scale.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO library_roots (id, path, added_at) VALUES (1, '/music', 1000);")));

    QVERIFY(q.exec(QStringLiteral(
        "WITH RECURSIVE cnt(x) AS ("
        "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 20000"
        ")"
        "INSERT INTO files (id, root_id, path, size, mtime, first_seen_at, scanned_at) "
        "SELECT x, 1, '/music/' || x || '.mp3', 1024, 1000, 1000, 1000 FROM cnt;")));

    QVERIFY(q.exec(QStringLiteral("WITH RECURSIVE cnt(x) AS ("
                                  "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 20000"
                                  ")"
                                  "INSERT INTO tracks (id, file_id, tags_read_at, created_at) "
                                  "SELECT x, x, 1000, 1000 FROM cnt;")));

    QVERIFY(q.exec(QStringLiteral(
        "WITH RECURSIVE cnt(x) AS ("
        "    SELECT 1 UNION ALL SELECT x+1 FROM cnt WHERE x < 20000"
        "),"
        "keys(k) AS ("
        "    SELECT 'TITLE' UNION ALL SELECT 'ARTIST' UNION ALL SELECT 'ALBUM' UNION ALL "
        "    SELECT 'ALBUMARTIST' UNION ALL SELECT 'GENRE' UNION ALL SELECT 'COMPOSER' UNION ALL "
        "    SELECT 'DATE' UNION ALL SELECT 'TRACKNUMBER' UNION ALL SELECT 'TRACKTOTAL' UNION ALL "
        "SELECT 'DISCNUMBER'"
        ")"
        "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
        "SELECT cnt.x, 'id3v2', 0, keys.k, 0, 'val_' || keys.k || '_' || cnt.x "
        "FROM cnt CROSS JOIN keys;")));

    QElapsedTimer timer;
    timer.start();

    QVERIFY(q.exec(QStringLiteral("BEGIN TRANSACTION;")));
    QVERIFY(q.exec(
        QStringLiteral("UPDATE tracks SET tags_read_at = 2000 WHERE id BETWEEN 10001 AND 11000;")));
    QVERIFY(q.exec(QStringLiteral("COMMIT;")));

    const qint64 elapsedMs = timer.elapsed();
    QVERIFY2(elapsedMs < 2000,
        qPrintable(
            QStringLiteral("Updating 1000 tracks took %1 ms, expected < 2000 ms").arg(elapsedMs)));

    // Spot-check results
    q.prepare(QStringLiteral(
        "SELECT title, artist, album, album_artist, genre, composer, year, track_number, "
        "track_total, disc_number FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(10500);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("val_TITLE_10500"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("val_ARTIST_10500"));
    QCOMPARE(q.value(2).toString(), QStringLiteral("val_ALBUM_10500"));
    QCOMPARE(q.value(3).toString(), QStringLiteral("val_ALBUMARTIST_10500"));
    QCOMPARE(q.value(4).toString(), QStringLiteral("val_GENRE_10500"));
    QCOMPARE(q.value(5).toString(), QStringLiteral("val_COMPOSER_10500"));
}

} // namespace

QTEST_GUILESS_MAIN(TstSchema)

#include "tst_Schema.moc"
