// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSet>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <butler/ArtistSplit.h>
#include <butler/ArtistSplitSource.h>
#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::ArtistSplitSource;
using linernotes::butler::SplitVerdict;
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

    static bool insertUserOverride(
        const QSqlDatabase &db, qint64 trackId, const QString &field, const QString &val)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO user_overrides (track_id, field, value, created_at, updated_at) "
            "VALUES (?, ?, ?, 1000, 1000);"));
        q.addBindValue(trackId);
        q.addBindValue(field);
        q.addBindValue(val);
        return q.exec();
    }
};

class TstArtistSplit : public QObject {
    Q_OBJECT

private slots:
    void decideSplit_data();
    void decideSplit();
    void weakSeparatorWithKnownParts();
    void sourceFindsCandidatesAndSkipsOverridden();
};

void TstArtistSplit::decideSplit_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<SplitVerdict>("expectedVerdict");
    QTest::addColumn<QStringList>("expectedParts");

    QTest::newRow("feat_with_dot_no_space")
        << QStringLiteral("Starving Trancer feat.Saori Hayami") << SplitVerdict::Split
        << QStringList { QStringLiteral("Starving Trancer"), QStringLiteral("Saori Hayami") };

    QTest::newRow("feat_with_space")
        << QStringLiteral("Yoshino Nanjo feat. yanaginagi") << SplitVerdict::Split
        << QStringList { QStringLiteral("Yoshino Nanjo"), QStringLiteral("yanaginagi") };

    QTest::newRow("multiply_no_space")
        << QStringLiteral("angela×fripSide") << SplitVerdict::Split
        << QStringList { QStringLiteral("angela"), QStringLiteral("fripSide") };

    QTest::newRow("multiply_with_space")
        << QStringLiteral("KOTOKO × LUNA") << SplitVerdict::Split
        << QStringList { QStringLiteral("KOTOKO"), QStringLiteral("LUNA") };

    QTest::newRow("semicolon_not_separator")
        << QStringLiteral("ave;new feat.C;LINE") << SplitVerdict::Split
        << QStringList { QStringLiteral("ave;new"), QStringLiteral("C;LINE") };

    QTest::newRow("pure_digits_keep")
        << QStringLiteral("22/7") << SplitVerdict::Keep << QStringList { QStringLiteral("22/7") };

    QTest::newRow("slash_without_space_ambiguous")
        << QStringLiteral("+α/Alfakyun.") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("+α"), QStringLiteral("Alfakyun.") };

    QTest::newRow("simon_and_garfunkel")
        << QStringLiteral("Simon & Garfunkel") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Simon"), QStringLiteral("Garfunkel") };

    QTest::newRow("earth_wind_and_fire")
        << QStringLiteral("Earth, Wind & Fire") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Earth"), QStringLiteral("Wind"), QStringLiteral("Fire") };

    QTest::newRow("florence_plus_the_machine")
        << QStringLiteral("Florence + the Machine") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Florence"), QStringLiteral("the Machine") };

    QTest::newRow("tom_petty_and_the_heartbreakers")
        << QStringLiteral("Tom Petty and the Heartbreakers") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Tom Petty"), QStringLiteral("the Heartbreakers") };

    QTest::newRow("wake_up_girls")
        << QStringLiteral("Wake Up, Girls!") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Wake Up"), QStringLiteral("Girls!") };

    QTest::newRow("comma_weak_separator_ambiguous")
        << QStringLiteral("Risa Taneda, Minori Chihara, Yuri Yamaoka") << SplitVerdict::Ambiguous
        << QStringList { QStringLiteral("Risa Taneda"), QStringLiteral("Minori Chihara"),
               QStringLiteral("Yuri Yamaoka") };

    QTest::newRow("cv_ambiguous") << QStringLiteral(
        "Mahiru Tsuyuzaki (CV: Haruki Iwata), Karen Aijo (CV: Momoyo Koyama)")
                                  << SplitVerdict::Ambiguous
                                  << QStringList { QStringLiteral(
                                                       "Mahiru Tsuyuzaki (CV: Haruki Iwata)"),
                                         QStringLiteral("Karen Aijo (CV: Momoyo Koyama)") };

    QTest::newRow("brackets_inside_keep")
        << QStringLiteral("sweet ARMS (Iori Nomizu, Misuzu Togashi, Kaori Sadohara, Misato)")
        << SplitVerdict::Keep
        << QStringList { QStringLiteral(
               "sweet ARMS (Iori Nomizu, Misuzu Togashi, Kaori Sadohara, Misato)") };

    QTest::newRow("multivalue_with_feat")
        << QStringLiteral("A / B feat. C") << SplitVerdict::Split
        << QStringList { QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C") };
}

void TstArtistSplit::decideSplit()
{
    QFETCH(QString, input);
    QFETCH(SplitVerdict, expectedVerdict);
    QFETCH(QStringList, expectedParts);

    const auto decision = linernotes::butler::decideSplit(input, { });
    QCOMPARE(decision.verdict, expectedVerdict);
    QCOMPARE(decision.parts, expectedParts);
}

void TstArtistSplit::weakSeparatorWithKnownParts()
{
    const QSet<QString> known = {
        QStringLiteral("Risa Taneda"),
        QStringLiteral("Minori Chihara"),
        QStringLiteral("Yuri Yamaoka"),
    };
    const auto decision = linernotes::butler::decideSplit(
        QStringLiteral("Risa Taneda, Minori Chihara, Yuri Yamaoka"), known);
    QCOMPARE(decision.verdict, SplitVerdict::Split);
    QCOMPARE(decision.parts,
        (QStringList {
            QStringLiteral("Risa Taneda"),
            QStringLiteral("Minori Chihara"),
            QStringLiteral("Yuri Yamaoka"),
        }));
    QCOMPARE(decision.confidence, 0.9);
}

void TstArtistSplit::sourceFindsCandidatesAndSkipsOverridden()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test_artist_split.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);
    QVERIFY(rootId > 0);

    // Track 1: A feat. B -> should be found as candidate (Split)
    const qint64 fileId1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/track1.mp3"));
    const qint64 trackId1 = TestDbHelper::insertTrack(conn, fileId1);
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId1, QStringLiteral("ARTIST"), QStringLiteral("A feat. B")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId1, QStringLiteral("ALBUM"), QStringLiteral("Album 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId1));

    // Track 2: Simon & Garfunkel -> Ambiguous, should also be found as candidate
    const qint64 fileId2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/track2.mp3"));
    const qint64 trackId2 = TestDbHelper::insertTrack(conn, fileId2);
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId2, QStringLiteral("ARTIST"), QStringLiteral("Simon & Garfunkel")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId2, QStringLiteral("ALBUM"), QStringLiteral("Album 2")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId2));

    // Track 3: C × D -> has user_override on artist, so should be skipped
    const qint64 fileId3
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/track3.mp3"));
    const qint64 trackId3 = TestDbHelper::insertTrack(conn, fileId3);
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId3, QStringLiteral("ARTIST"), QStringLiteral("C × D")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, trackId3, QStringLiteral("ALBUM"), QStringLiteral("Album 3")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, trackId3));
    QVERIFY(TestDbHelper::insertUserOverride(
        conn, trackId3, QStringLiteral("artist"), QStringLiteral("C × D")));

    ArtistSplitSource source(db);
    const auto itemsRes = source.findItems();
    QVERIFY(itemsRes.ok());
    const auto &itemKeys = itemsRes.value();

    QCOMPARE(itemKeys.size(), 1);
    const QString &key = itemKeys.at(0);
    QVERIFY(key.contains(QStringLiteral("A feat. B")));
    QVERIFY(key.contains(QStringLiteral("Simon & Garfunkel")));
    QVERIFY(!key.contains(QStringLiteral("C × D")));

    const auto groupRes = source.loadItem(key);
    QVERIFY(groupRes.ok());
    const auto &group = groupRes.value();
    QCOMPARE(group.candidates.size(), 2);
    QCOMPARE(group.candidates.at(0).original, QStringLiteral("A feat. B"));
    QCOMPARE(group.candidates.at(0).targets.size(), 1);
    QCOMPARE(group.candidates.at(0).targets.at(0).trackId, trackId1);
    QCOMPARE(group.candidates.at(1).original, QStringLiteral("Simon & Garfunkel"));
    QCOMPARE(group.candidates.at(1).targets.size(), 1);
    QCOMPARE(group.candidates.at(1).targets.at(0).trackId, trackId2);
}

} // namespace

QTEST_GUILESS_MAIN(TstArtistSplit)

#include "tst_ArtistSplit.moc"
