// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSet>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

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

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId,
        const QVariant &cueIndex = QVariant(), const QVariant &albumId = QVariant(),
        qint64 tagsReadAt = 0)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, ?, ?, ?, 3000);"));
        q.addBindValue(fileId);
        q.addBindValue(cueIndex.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : cueIndex);
        q.addBindValue(albumId.isNull() ? QVariant(QMetaType(QMetaType::LongLong)) : albumId);
        q.addBindValue(tagsReadAt);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
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
    void cascadeDeletes();
    void effectiveLayeringPrecedence();
    void effectiveMetadataParsingAndPriorities();
    void triggersRefreshAndSearchDirty();
};

void TstSchema::migratesFreshDatabaseToLatest()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("fresh.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    QCOMPARE(Migrator::currentVersion(conn).value(), 8);

    // Verify core tables exist
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT name FROM sqlite_schema WHERE type='table';")));
    QSet<QString> tables;
    while (q.next()) {
        tables.insert(q.value(0).toString());
    }
    QVERIFY(tables.contains(QStringLiteral("tracks")));
    QVERIFY(tables.contains(QStringLiteral("effective_metadata")));
    QVERIFY(tables.contains(QStringLiteral("search_index")));
    QVERIFY(tables.contains(QStringLiteral("search_dirty")));
    QVERIFY(tables.contains(QStringLiteral("track_play_stats")));

    // Integrity check
    QVERIFY(q.exec(QStringLiteral("PRAGMA integrity_check;")));
    QVERIFY(q.next() && q.value(0).toString() == QStringLiteral("ok"));
}

void TstSchema::cascadeDeletes()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("cascade.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("INSERT INTO albums (id, grouping_key, title, created_at) VALUES "
                                  "(10, 'k10', 'Album X', 100);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO artists (id, name, created_at) VALUES (20, 'Artist Y', 100);")));

    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, QVariant(), 10, 100);
    QVERIFY(trackId > 0);
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Cascade Title")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 200));

    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO track_artists (track_id, artist_id, role) VALUES (1, 20, 'artist');")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO favorites (entity_type, entity_id, created_at) VALUES ('track', 1, 100);")));
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO play_events (id, track_id, started_at) VALUES (40, 1, 100);")));

    // Delete file -> cascades to track, raw_tags, effective_metadata, track_artists, favorites
    QVERIFY(q.exec(QStringLiteral("DELETE FROM files WHERE id = 1;")));

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM effective_metadata WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);
    QVERIFY(q.exec(QStringLiteral(
        "SELECT COUNT(*) FROM favorites WHERE entity_type = 'track' AND entity_id = 1;")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    // play_events track_id set to NULL
    QVERIFY(q.exec(QStringLiteral("SELECT track_id FROM play_events WHERE id = 40;")));
    QVERIFY(q.next() && q.value(0).isNull());
}

void TstSchema::effectiveLayeringPrecedence()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("layering.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    auto getEffectiveTitle = [&]() -> QVariant {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
        q.addBindValue(trackId);
        return (q.exec() && q.next()) ? q.value(0) : QVariant();
    };

    // 1. Raw tag layer
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Raw Title")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 100));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Raw Title"));

    // 2. Pending correction has no effect
    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO corrections (id, entity_type, entity_id, field, new_value, "
                       "source, confidence, status, created_at) "
                       "VALUES (1, 'track', 1, 'title', 'Corr 1', 'rule', 0.9, 'pending', 200);")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Raw Title"));

    // 3. Accepted correction takes precedence over raw tag
    QVERIFY(q.exec(QStringLiteral(
        "UPDATE corrections SET status = 'accepted', decided_at = 300 WHERE id = 1;")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Corr 1"));

    // 4. User override takes highest precedence
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'title', 'Override Title', 500, 500);")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Override Title"));

    // 5. Deleting user override reverts back to accepted correction
    QVERIFY(q.exec(
        QStringLiteral("DELETE FROM user_overrides WHERE track_id = 1 AND field = 'title';")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Corr 1"));

    // 6. Reverting correction reverts back to raw tag
    QVERIFY(q.exec(QStringLiteral("UPDATE corrections SET status = 'reverted' WHERE id = 1;")));
    QCOMPARE(getEffectiveTitle().toString(), QStringLiteral("Raw Title"));
}

void TstSchema::effectiveMetadataParsingAndPriorities()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("parsing.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    // id3v2 priority 0 vs id3v1 priority 9
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Song Title")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 0, QStringLiteral("Artist A")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("ARTIST"), 1, QStringLiteral("Artist B")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v1"), 9,
        QStringLiteral("ARTIST"), 0, QStringLiteral("V1 Artist")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v1"), 9,
        QStringLiteral("GENRE"), 0, QStringLiteral("Rock")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("DATE"), 0, QStringLiteral("2003-07-31")));
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TRACKNUMBER"), 0, QStringLiteral("3/12")));

    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 2000));

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT title, artist, genre, year, track_number, track_total FROM "
                             "effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());

    QCOMPARE(q.value(0).toString(), QStringLiteral("Song Title"));
    QCOMPARE(q.value(1).toString(), QStringLiteral("Artist A / Artist B"));
    QCOMPARE(q.value(2).toString(), QStringLiteral("Rock"));
    QCOMPARE(q.value(3).toInt(), 2003);
    QCOMPARE(q.value(4).toInt(), 3);
    QCOMPARE(q.value(5).toInt(), 12);
}

void TstSchema::triggersRefreshAndSearchDirty()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    Database db(dbDir.filePath(QStringLiteral("refresh_dirty.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId, QVariant(), QVariant(), 100);

    // Insert raw tag before tags_read_at update -> effective_metadata title is still NULL
    QVERIFY(TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("id3v2"), 0,
        QStringLiteral("TITLE"), 0, QStringLiteral("Refreshed Title")));
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next() && q.value(0).isNull());

    // Updating tags_read_at triggers refresh of effective_metadata
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId, 200));
    q.prepare(QStringLiteral("SELECT title FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("Refreshed Title"));

    // Check search_dirty is marked for this track
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM search_dirty WHERE track_id = ?;"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next() && q.value(0).toInt() == 1);
}

} // namespace

QTEST_GUILESS_MAIN(TstSchema)

#include "tst_Schema.moc"
