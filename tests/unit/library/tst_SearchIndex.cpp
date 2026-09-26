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
    static qint64 insertTrack(const QSqlDatabase &db, const QString &title,
        const QString &artist = QString(), const QString &album = QString())
    {
        static int s_trackCounter = 0;
        const int idx = ++s_trackCounter;
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "INSERT OR IGNORE INTO library_roots (path, added_at) VALUES ('/m', 1000);"));
        q.exec(QStringLiteral("SELECT id FROM library_roots WHERE path = '/m';"));
        qint64 rootId = 1;
        if (q.next()) {
            rootId = q.value(0).toLongLong();
        }

        q.prepare(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, first_seen_at, "
                                 "scanned_at) VALUES (?, ?, 1024, 1000, 1000, 1000);"));
        q.addBindValue(rootId);
        q.addBindValue(QStringLiteral("/m/s_%1.mp3").arg(idx));
        q.exec();
        const qint64 fileId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                                 "VALUES (?, 1000, 1000);"));
        q.addBindValue(fileId);
        q.exec();
        const qint64 trackId = q.lastInsertId().toLongLong();

        auto addTag = [&](const QString &k, const QString &v) {
            if (!v.isEmpty()) {
                q.prepare(QStringLiteral(
                    "INSERT INTO raw_tags (track_id, tag_type, priority, key, ordinal, value) "
                    "VALUES (?, 'id3v2', 0, ?, 0, ?);"));
                q.addBindValue(trackId);
                q.addBindValue(k);
                q.addBindValue(v);
                q.exec();
            }
        };
        addTag(QStringLiteral("TITLE"), title);
        addTag(QStringLiteral("ARTIST"), artist);
        addTag(QStringLiteral("ALBUM"), album);

        q.prepare(QStringLiteral("UPDATE tracks SET tags_read_at = 2000 WHERE id = ?;"));
        q.addBindValue(trackId);
        q.exec();

        return trackId;
    }
};

class TstSearchIndex : public QObject {
    Q_OBJECT

private slots:
    void substringAndPinyinMatching();
    void simplifiedTraditionalAndKanaSearch();
    void rankingPrefersTitleOverAlbum();
    void updatesAndDeletionsReindex();
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

    const qint64 trackId = TestHelper::insertTrack(
        qDb, QStringLiteral("林晓风"), QStringLiteral("夜行者"), QStringLiteral("山谷的回响"));

    SearchIndex index(qDb);
    const auto flushRes = index.flushDirty();
    QVERIFY(flushRes.ok());
    QCOMPARE(flushRes.value(), 1);

    // Chinese substring, full pinyin, and initials matching
    for (const auto &query : { QStringLiteral("晓风"), QStringLiteral("lin xiao"),
             QStringLiteral("linxi"), QStringLiteral("lxf") }) {
        const auto hits = index.search(query);
        QVERIFY(hits.ok());
        QCOMPARE(hits.value().size(), 1);
        QCOMPARE(hits.value().first().trackId, trackId);
    }
}

void TstSearchIndex::simplifiedTraditionalAndKanaSearch()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_multilingual.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 track1 = TestHelper::insertTrack(qDb, QStringLiteral("晚风里的歌"));
    const qint64 track2 = TestHelper::insertTrack(qDb, QStringLiteral("藍天白雲"));
    const qint64 track3
        = TestHelper::insertTrack(qDb, QStringLiteral("さくら"), QStringLiteral("Hello Band"));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    // Search Simplified with Traditional query
    auto hits = index.search(QStringLiteral("晚風"));
    QVERIFY(hits.ok() && hits.value().size() == 1);
    QCOMPARE(hits.value().first().trackId, track1);

    // Search Traditional with Simplified query
    hits = index.search(QStringLiteral("蓝天"));
    QVERIFY(hits.ok() && hits.value().size() == 1);
    QCOMPARE(hits.value().first().trackId, track2);

    // Japanese kana romaji & English multi-word
    hits = index.search(QStringLiteral("sakura band"));
    QVERIFY(hits.ok() && hits.value().size() == 1);
    QCOMPARE(hits.value().first().trackId, track3);
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

    const qint64 track1 = TestHelper::insertTrack(
        qDb, QStringLiteral("夜空中最亮的星"), QString(), QStringLiteral("普通专辑"));
    const qint64 track2 = TestHelper::insertTrack(
        qDb, QStringLiteral("其他歌曲"), QString(), QStringLiteral("夜空中最亮的星"));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    const auto hits = index.search(QStringLiteral("最亮的星"));
    QVERIFY(hits.ok());
    QCOMPARE(hits.value().size(), 2);
    // Track 1 (title match) must be ranked before Track 2 (album match)
    QCOMPARE(hits.value().at(0).trackId, track1);
    QCOMPARE(hits.value().at(1).trackId, track2);
}

void TstSearchIndex::updatesAndDeletionsReindex()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("search_updates.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &qDb = connRes.value();

    const qint64 trackId = TestHelper::insertTrack(qDb, QStringLiteral("旧标题"));

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());
    QCOMPARE(index.search(QStringLiteral("旧标题")).value().size(), 1);

    // User override updates search index
    QSqlQuery q(qDb);
    q.prepare(QStringLiteral(
        "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
        "VALUES (?, 'title', '新标题', 3000, 3000);"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());
    QVERIFY(index.flushDirty().ok());

    QCOMPARE(index.search(QStringLiteral("旧标题")).value().size(), 0);
    const auto hitsNew = index.search(QStringLiteral("新标题"));
    QVERIFY(hitsNew.ok());
    QCOMPARE(hitsNew.value().size(), 1);
    QCOMPARE(hitsNew.value().first().trackId, trackId);

    // Deletion removes from search index
    q.prepare(
        QStringLiteral("DELETE FROM files WHERE id = (SELECT file_id FROM tracks WHERE id = ?);"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());
    QCOMPARE(index.search(QStringLiteral("新标题")).value().size(), 0);
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

    const qint64 trackId
        = TestHelper::insertTrack(qDb, QStringLiteral("晨曦微光"), QStringLiteral("林晓风"));

    QSqlQuery q(qDb);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO artists (id, name, created_at) VALUES (10, '林晓风', 1000);")));
    q.prepare(QStringLiteral(
        "INSERT INTO track_artists (track_id, artist_id, role) VALUES (?, 10, 'artist');"));
    q.addBindValue(trackId);
    QVERIFY(q.exec());

    SearchIndex index(qDb);
    QVERIFY(index.flushDirty().ok());

    // Add alias for artist 10
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO artist_aliases (artist_id, alias, kind, source, created_at) "
                       "VALUES (10, 'Lin Xiaofeng', 'romanization', 'rule', 2000);")));

    QVERIFY(index.flushDirty().ok());

    const auto hits = index.search(QStringLiteral("Xiaofeng"));
    QVERIFY(hits.ok());
    QCOMPARE(hits.value().size(), 1);
    QCOMPARE(hits.value().first().trackId, trackId);
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
        QVERIFY2(res.ok(), qPrintable(QStringLiteral("Failed on input '%1'").arg(input)));
    }
}

} // namespace

QTEST_GUILESS_MAIN(TstSearchIndex)

#include "tst_SearchIndex.moc"
