// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <library/Database.h>
#include <library/Migrator.h>
#include <library/SearchIndex.h>

namespace {

using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::library::SearchIndex;

struct TestHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path = QStringLiteral("/music"))
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, 1000);"));
        q.addBindValue(path);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1024, 1000, 1000, 1000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 fileId)
    {
        QSqlQuery q(db);
        q.prepare(
            QStringLiteral("INSERT INTO tracks (file_id, cue_index, tags_read_at, created_at) "
                           "VALUES (?, NULL, 1000, 1000);"));
        q.addBindValue(fileId);
        if (!q.exec()) {
            return -1;
        }
        return q.lastInsertId().toLongLong();
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

    static bool updateTagsReadAt(const QSqlDatabase &db, qint64 trackId, qint64 time = 2000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = ? WHERE id = ?;"));
        q.addBindValue(time);
        q.addBindValue(trackId);
        return q.exec();
    }
};

class TstSearchIndex : public QObject {
    Q_OBJECT

private slots:
    void substringAndPinyinMatching();
    void simplifiedAndTraditionalCrossSearch();
    void kanaAndEnglishPrefixAndMultiWord();
    void rankingPrefersTitleOverAlbum();
    void userOverridesReindex();
    void cascadeDeleteRemovesFromIndex();
    void artistAliasesSearch();
    void specialCharactersDoNotError();
};

void TstSearchIndex::substringAndPinyinMatching()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);
    const qint64 fileId = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 trackId = TestHelper::insertTrack(qDb, fileId);

    QVERIFY(
        TestHelper::insertRawTag(qDb, trackId, QStringLiteral("TITLE"), QStringLiteral("林晓风")));
    QVERIFY(
        TestHelper::insertRawTag(qDb, trackId, QStringLiteral("ARTIST"), QStringLiteral("夜行者")));
    QVERIFY(TestHelper::insertRawTag(
        qDb, trackId, QStringLiteral("ALBUM"), QStringLiteral("山谷的回响")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, trackId));

    SearchIndex index(qDb);
    const auto flushRes = index.flushDirty();
    QVERIFY(flushRes.ok());
    QCOMPARE(flushRes.value(), 1);

    // 1. Chinese title 2-char substring
    {
        const auto hits = index.search(QStringLiteral("晓风"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, trackId);
    }

    // 2. Full pinyin with spaces
    {
        const auto hits = index.search(QStringLiteral("lin xiao"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, trackId);
    }

    // 3. Pinyin joined prefix
    {
        const auto hits = index.search(QStringLiteral("linxi"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, trackId);
    }

    // 4. Pinyin initials
    {
        const auto hits = index.search(QStringLiteral("lxf"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, trackId);
    }
}

void TstSearchIndex::simplifiedAndTraditionalCrossSearch()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_hans_hant.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);

    // Track 1: Simplified indexed
    const qint64 file1 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 track1 = TestHelper::insertTrack(qDb, file1);
    QVERIFY(TestHelper::insertRawTag(
        qDb, track1, QStringLiteral("TITLE"), QStringLiteral("晚风里的歌")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track1));

    // Track 2: Traditional indexed
    const qint64 file2 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song2.mp3"));
    const qint64 track2 = TestHelper::insertTrack(qDb, file2);
    QVERIFY(
        TestHelper::insertRawTag(qDb, track2, QStringLiteral("TITLE"), QStringLiteral("藍天白雲")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track2));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    // Search Simplified with Traditional query "晚風"
    {
        const auto hits = index.search(QStringLiteral("晚風"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, track1);
    }

    // Search Traditional with Simplified query "蓝天"
    {
        const auto hits = index.search(QStringLiteral("蓝天"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, track2);
    }
}

void TstSearchIndex::kanaAndEnglishPrefixAndMultiWord()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_kana.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);
    const qint64 file1 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 track1 = TestHelper::insertTrack(qDb, file1);
    QVERIFY(
        TestHelper::insertRawTag(qDb, track1, QStringLiteral("TITLE"), QStringLiteral("さくら")));
    QVERIFY(TestHelper::insertRawTag(
        qDb, track1, QStringLiteral("ARTIST"), QStringLiteral("Hello Band")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track1));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    // Japanese kana romaji
    {
        const auto hits = index.search(QStringLiteral("sakura"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, track1);
    }

    // English prefix match
    {
        const auto hits = index.search(QStringLiteral("hel"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, track1);
    }

    // Multi-word AND
    {
        const auto hits = index.search(QStringLiteral("sakura band"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, track1);
    }
}

void TstSearchIndex::rankingPrefersTitleOverAlbum()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_rank.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);

    // Track 1: title matches query
    const qint64 file1 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 track1 = TestHelper::insertTrack(qDb, file1);
    QVERIFY(TestHelper::insertRawTag(
        qDb, track1, QStringLiteral("TITLE"), QStringLiteral("夜空中最亮的星")));
    QVERIFY(
        TestHelper::insertRawTag(qDb, track1, QStringLiteral("ALBUM"), QStringLiteral("普通专辑")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track1));

    // Track 2: only album matches query
    const qint64 file2 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song2.mp3"));
    const qint64 track2 = TestHelper::insertTrack(qDb, file2);
    QVERIFY(
        TestHelper::insertRawTag(qDb, track2, QStringLiteral("TITLE"), QStringLiteral("其他歌曲")));
    QVERIFY(TestHelper::insertRawTag(
        qDb, track2, QStringLiteral("ALBUM"), QStringLiteral("夜空中最亮的星")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track2));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    const auto hits = index.search(QStringLiteral("最亮的星"));
    QVERIFY(hits.ok());
    QCOMPARE(hits.value().size(), 2);
    // Track 1 (title match) must be ranked before Track 2 (album match)
    QCOMPARE(hits.value().at(0).trackId, track1);
    QCOMPARE(hits.value().at(1).trackId, track2);
}

void TstSearchIndex::userOverridesReindex()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_override.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);
    const qint64 file1 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 track1 = TestHelper::insertTrack(qDb, file1);
    QVERIFY(
        TestHelper::insertRawTag(qDb, track1, QStringLiteral("TITLE"), QStringLiteral("旧标题")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track1));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    // Initially matches old title
    {
        const auto hits = index.search(QStringLiteral("旧标题"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
    }

    // Insert user override
    QSqlQuery q(qDb);
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (1, 'title', '新标题', 3000, 3000);")));

    // Flush dirty
    QVERIFY(index.flushDirty().ok());

    // Old title no longer matches, new title matches
    {
        const auto hitsOld = index.search(QStringLiteral("旧标题"));
        QVERIFY(hitsOld.ok());
        QCOMPARE(hitsOld.value().size(), 0);

        const auto hitsNew = index.search(QStringLiteral("新标题"));
        QVERIFY(hitsNew.ok());
        QCOMPARE(hitsNew.value().size(), 1);
        QCOMPARE(hitsNew.value().first().trackId, track1);
    }
}

void TstSearchIndex::cascadeDeleteRemovesFromIndex()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_del.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);
    const qint64 file1 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 track1 = TestHelper::insertTrack(qDb, file1);
    QVERIFY(TestHelper::insertRawTag(
        qDb, track1, QStringLiteral("TITLE"), QStringLiteral("将要删除的歌曲")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track1));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    {
        const auto hits = index.search(QStringLiteral("将要删除"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
    }

    // Delete file (cascades to tracks, effective_metadata, and trigger deletes from search_index)
    QSqlQuery q(qDb);
    QVERIFY(q.exec(QStringLiteral("DELETE FROM files WHERE id = 1;")));

    {
        const auto hits = index.search(QStringLiteral("将要删除"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 0);
    }
}

void TstSearchIndex::artistAliasesSearch()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_alias.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 rootId = TestHelper::insertRoot(qDb);
    const qint64 file1 = TestHelper::insertFile(qDb, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 track1 = TestHelper::insertTrack(qDb, file1);
    QVERIFY(
        TestHelper::insertRawTag(qDb, track1, QStringLiteral("TITLE"), QStringLiteral("晨曦微光")));
    QVERIFY(
        TestHelper::insertRawTag(qDb, track1, QStringLiteral("ARTIST"), QStringLiteral("林晓风")));
    QVERIFY(TestHelper::updateTagsReadAt(qDb, track1));

    QSqlQuery q(qDb);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO artists (id, name, created_at) VALUES (10, '林晓风', 1000);")));
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO track_artists (track_id, artist_id, role) VALUES (1, 10, 'artist');")));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    // Add alias for artist 10
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO artist_aliases (artist_id, alias, kind, source, created_at) "
                       "VALUES (10, 'Lin Xiaofeng', 'romanization', 'rule', 2000);")));

    // Flush dirty
    QVERIFY(index.flushDirty().ok());

    // Search by alias
    {
        const auto hits = index.search(QStringLiteral("Xiaofeng"));
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, track1);
    }
}

void TstSearchIndex::specialCharactersDoNotError()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_special.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    SearchIndex index(qDb);

    const QStringList specialInputs = {
        QStringLiteral("\""),
        QStringLiteral("*"),
        QStringLiteral("-"),
        QStringLiteral("AND"),
        QStringLiteral("OR"),
        QStringLiteral("NOT"),
        QStringLiteral("NEAR(5)"),
        QStringLiteral(":::"),
        QStringLiteral("^"),
        QStringLiteral("track:01 - live*"),
        QStringLiteral("(((("),
        QStringLiteral("\"\"\"\""),
        QStringLiteral(""),
        QStringLiteral("    \t\n  "),
    };

    for (const auto &input : specialInputs) {
        const auto res = index.search(input);
        if (!res.ok()) {
            QFAIL(qPrintable(
                QStringLiteral("Failed on input '%1': %2").arg(input, res.error().toString())));
        }
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstSearchIndex)

#include "tst_SearchIndex.moc"
