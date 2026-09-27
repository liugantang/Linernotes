// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Errors.h>
#include <library/LibraryQuery.h>
#include <library/Migrator.h>
#include <library/OverrideStore.h>

namespace {

using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::LibraryQuery;
using linernotes::library::Migrator;
using linernotes::library::OverrideStore;
using linernotes::library::TagEdit;
using linernotes::library::TagField;
using linernotes::library::TrackFilter;
using linernotes::library::TrackRow;
using linernotes::library::TrackSortKey;

/// 库中只有一首曲目：经 LibraryQuery 读回它
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

class TstOverrideStore : public QObject {
    Q_OBJECT

private slots:
    void titleOverrideAndRevert();
    void albumOverrideRelinksAndCleansOrphans();
    void invalidYearReturnsErrorAndKeepsDbIntact();
};

void TstOverrideStore::titleOverrideAndRevert()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_override.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Original Title")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Original Artist")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Original Album")));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("DATE"), QStringLiteral("2020")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    OverrideStore store(db);
    const auto initialValsRes = store.effectiveValues(trackId);
    QVERIFY(initialValsRes.ok());
    QCOMPARE(initialValsRes.value().value(TagField::Title), QStringLiteral("Original Title"));

    // 1. Override Title
    const auto applyRes = store.apply({ trackId },
        { TagEdit { .field = TagField::Title, .value = QStringLiteral("New Title") } });
    QVERIFY(applyRes.ok());

    const auto modifiedValsRes = store.effectiveValues(trackId);
    QVERIFY(modifiedValsRes.ok());
    QCOMPARE(modifiedValsRes.value().value(TagField::Title), QStringLiteral("New Title"));

    QCOMPARE(onlyTrack(db).title, QStringLiteral("New Title"));

    const auto overriddenRes = store.overriddenFields(trackId);
    QVERIFY(overriddenRes.ok());
    QVERIFY(overriddenRes.value().contains(TagField::Title));

    // 2. Revert Title (nullopt)
    const auto revertRes
        = store.apply({ trackId }, { TagEdit { .field = TagField::Title, .value = std::nullopt } });
    QVERIFY(revertRes.ok());

    const auto revertedValsRes = store.effectiveValues(trackId);
    QVERIFY(revertedValsRes.ok());
    QCOMPARE(revertedValsRes.value().value(TagField::Title), QStringLiteral("Original Title"));

    QCOMPARE(onlyTrack(db).title, QStringLiteral("Original Title"));

    const auto overriddenRes2 = store.overriddenFields(trackId);
    QVERIFY(overriddenRes2.ok());
    QVERIFY(!overriddenRes2.value().contains(TagField::Title));
}

void TstOverrideStore::albumOverrideRelinksAndCleansOrphans()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_album_override.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/album1/song.mp3"));
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ARTIST"), QStringLiteral("Band 1")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUM"), QStringLiteral("Old Album")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("ALBUMARTIST"), QStringLiteral("Band 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = 'Old Album'")));
    QVERIFY(q.next() && q.value(0).toInt() == 1);

    OverrideStore store(db);
    const auto applyRes = store.apply({ trackId },
        { TagEdit { .field = TagField::Album, .value = QStringLiteral("New Album") } });
    QVERIFY(applyRes.ok());

    // Old album should be removed by removeOrphans, and New Album created
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = 'Old Album'")));
    QVERIFY(q.next() && q.value(0).toInt() == 0);

    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = 'New Album'")));
    QVERIFY(q.next() && q.value(0).toInt() == 1);

    QCOMPARE(onlyTrack(db).album, QStringLiteral("New Album"));
}

void TstOverrideStore::invalidYearReturnsErrorAndKeepsDbIntact()
{
    const QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());
    Database db(dbDir.filePath(QStringLiteral("test_invalid_year.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    const qint64 fileId = TestDbHelper::insertFile(conn, rootId);
    const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("DATE"), QStringLiteral("2020")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(trackId).ok());

    OverrideStore store(db);
    const auto errRes = store.apply(
        { trackId }, { TagEdit { .field = TagField::Year, .value = QStringLiteral("abc") } });
    QVERIFY(!errRes.ok());
    QCOMPARE(errRes.error().code, QString(errc::kTagOverrideInvalid));

    const auto valRes = store.effectiveValues(trackId);
    QVERIFY(valRes.ok());
    QCOMPARE(valRes.value().value(TagField::Year), QStringLiteral("2020"));

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM user_overrides WHERE track_id = ?"));
    q.addBindValue(trackId);
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toInt(), 0);
}

} // namespace

QTEST_MAIN(TstOverrideStore)
#include "tst_OverrideStore.moc"
