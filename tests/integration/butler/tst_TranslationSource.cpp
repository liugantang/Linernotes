// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/TranslationSource.h>
#include <butler/TranslationStore.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::parseTranslationItemKey;
using linernotes::butler::TranslationSource;
using linernotes::butler::TranslationStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
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
};

class TstTranslationSource : public QObject {
    Q_OBJECT

private slots:
    void findPendingSaveAndLookup();
};

void TstTranslationSource::findPendingSaveAndLookup()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_translation_source.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    // Track 1: TITLE = "Snow halation", ALBUM = "Snow halation" (duplicates)
    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Snow halation"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ALBUM"), QStringLiteral("Snow halation"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    // Track 2: TITLE = "炎", ALBUM = "炎" (hanzi only, needsTranslation == false)
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.mp3"));
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("炎"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ALBUM"), QStringLiteral("炎"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    // Track 3: TITLE = "あぁ光塚歌劇団"
    const qint64 f3 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/3.mp3"));
    const qint64 t3 = TestDbHelper::insertTrack(conn, f3);
    TestDbHelper::insertRawTag(conn, t3, QStringLiteral("TITLE"), QStringLiteral("あぁ光塚歌劇団"));
    TestDbHelper::updateTagsReadAt(conn, t3);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());
    QVERIFY(linker.linkTrack(t3).ok());

    const TranslationSource source(db);

    // 1. Pending should contain 2 items: "Snow halation" and "あぁ光塚歌劇団"
    // (Snow halation deduplicated across title and album; 炎 filtered out because only Hanzi)
    const auto countRes = source.countPending(1);
    QVERIFY(countRes.ok());
    QCOMPARE(countRes.value(), 2);

    const auto itemsRes = source.findItems(1);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();
    QCOMPARE(items.size(), 1);

    const auto samplesRes = parseTranslationItemKey(items.first());
    QVERIFY(samplesRes.ok());
    const auto &samples = samplesRes.value();
    QCOMPARE(samples.size(), 2);
    QVERIFY(samples.contains(QStringLiteral("Snow halation")));
    QVERIFY(samples.contains(QStringLiteral("あぁ光塚歌劇団")));

    // 2. Save translation for "Snow halation"
    const ManualClock clock(1000);
    const TranslationStore store(db, clock);

    QHash<QString, QString> saveMap;
    saveMap.insert(QStringLiteral("Snow halation"), QStringLiteral("雪之光晕"));
    const auto saveRes = store.save(saveMap, QStringLiteral("test-model"), 1);
    QVERIFY(saveRes.ok());

    // 3. After saving 1 item, only 1 item ("あぁ光塚歌劇団") is pending for prompt version 1
    QCOMPARE(source.countPending(1).value(), 1);

    const auto updatedItems = source.findItems(1).value();
    QCOMPARE(updatedItems.size(), 1);
    const auto updatedSamples = parseTranslationItemKey(updatedItems.first()).value();
    QCOMPARE(updatedSamples.size(), 1);
    QCOMPARE(updatedSamples.first(), QStringLiteral("あぁ光塚歌劇団"));

    // Also save empty translation for another item to verify lookup filters it out
    const auto saveEmptyRes = store.save(
        { { QStringLiteral("EVA-01"), QStringLiteral("") } }, QStringLiteral("test-model"), 1);
    QVERIFY(saveEmptyRes.ok());

    // 4. Test lookup: only non-empty translations are returned
    const auto lookupRes = store.lookup({
        QStringLiteral("Snow halation"),
        QStringLiteral("EVA-01"),
        QStringLiteral("あぁ光塚歌劇団"),
        QStringLiteral("Nonexistent"),
    });
    QVERIFY(lookupRes.ok());
    const auto &lookupMap = lookupRes.value();
    QCOMPARE(lookupMap.size(), 1);
    QVERIFY(lookupMap.contains(QStringLiteral("Snow halation")));
    QCOMPARE(lookupMap.value(QStringLiteral("Snow halation")), QStringLiteral("雪之光晕"));
    QVERIFY(!lookupMap.contains(QStringLiteral("EVA-01")));
    QVERIFY(!lookupMap.contains(QStringLiteral("あぁ光塚歌劇団")));
    QVERIFY(!lookupMap.contains(QStringLiteral("Nonexistent")));
}

} // namespace

QTEST_GUILESS_MAIN(TstTranslationSource)

#include "tst_TranslationSource.moc"
