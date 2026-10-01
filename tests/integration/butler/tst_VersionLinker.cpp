// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/ArtistName.h>
#include <butler/TitleMatchStore.h>
#include <butler/TitleVersion.h>
#include <butler/VersionLinker.h>
#include <butler/VersionSuffixStore.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>

#include <tuple>
#include <utility>

namespace {

using linernotes::butler::exactKey;
using linernotes::butler::SuffixClass;
using linernotes::butler::suffixKey;
using linernotes::butler::SuffixRole;
using linernotes::butler::SuffixVerdict;
using linernotes::butler::TitleMatchStore;
using linernotes::butler::TitleMatchVerdict;
using linernotes::butler::VersionLinker;
using linernotes::butler::VersionSuffixStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::library::VersionType;
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

class TstVersionLinker : public QObject {
    Q_OBJECT

private slots:
    void linksVersionsAndHandlesOrphans();
    void linksCrossScriptTitlesWhenMatched();
};

void TstVersionLinker::linksVersionsAndHandlesOrphans()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_version_linker.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    struct TrackSpec {
        QString artist;
        QString title;
    };
    const QList<TrackSpec> trackSpecs = {
        { .artist = QStringLiteral("Artist A"), .title = QStringLiteral("Snow halation") },
        { .artist = QStringLiteral("Artist A"),
            .title = QStringLiteral("Snow halation (Extended Mix)") },
        { .artist = QStringLiteral("Artist A"),
            .title = QStringLiteral("Snow halation (Instrumental)") },
        { .artist = QStringLiteral("Artist A"), .title = QStringLiteral("Song (Elite)") },
        { .artist = QStringLiteral("Artist A"),
            .title = QStringLiteral("Other (Forget about my love)") },
        { .artist = QStringLiteral("Artist B"), .title = QStringLiteral("Snow halation") },
    };

    QList<qint64> trackIds;
    EntityLinker linker(conn);
    for (qsizetype i = 0; i < trackSpecs.size(); ++i) {
        const QString filePath = QStringLiteral("/music/%1.mp3").arg(i + 1);
        const qint64 fileId = TestDbHelper::insertFile(conn, rootId, filePath);
        const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);
        trackIds.append(trackId);

        TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("TITLE"), trackSpecs.at(i).title);
        TestDbHelper::insertRawTag(
            conn, trackId, QStringLiteral("ARTIST"), trackSpecs.at(i).artist);
        TestDbHelper::updateTagsReadAt(conn, trackId);
        QVERIFY(linker.linkTrack(trackId).ok());
    }

    const ManualClock clock(1000);
    const VersionSuffixStore store(db, clock);

    // Pre-save Elite -> title_part
    const QString keyElite = suffixKey(QStringLiteral("Elite"));
    const SuffixVerdict verdictElite {
        .cls = SuffixClass {
            .role = SuffixRole::TitlePart,
            .type = VersionType::Studio,
        },
        .confidence = 0.95,
        .reason = QStringLiteral("Part of title"),
    };
    QVERIFY(store.save({ { keyElite, verdictElite } }, QStringLiteral("test-model"), 1).ok());

    const VersionLinker versionLinker(db, clock);

    // 1. Initial countPending() before linkAll should be 6
    const auto initialPendingRes = versionLinker.countPending();
    QVERIFY(initialPendingRes.ok());
    QCOMPARE(initialPendingRes.value(), 6);

    // 2. First linkAll(1, 0)
    const auto linkRes1 = versionLinker.linkAll(1, 0);
    QVERIFY(linkRes1.ok());
    const auto &stats1 = linkRes1.value();

    QCOMPARE(stats1.tracks, 6);
    QCOMPARE(stats1.works, 4);
    QCOMPARE(stats1.unresolved, 1);

    auto getTrackWorkId = [&](qint64 tId) -> qint64 {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT work_id FROM tracks WHERE id = ?;"));
        q.addBindValue(tId);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    };

    auto getTrackVersion = [&](qint64 tId) -> std::tuple<QString, QString, int> {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral(
            "SELECT base_title, version_type, unresolved FROM track_versions WHERE track_id = ?;"));
        q.addBindValue(tId);
        if (q.exec() && q.next()) {
            return { q.value(0).toString(), q.value(1).toString(), q.value(2).toInt() };
        }
        return { };
    };

    auto getWorkTitle = [&](qint64 wId) -> QString {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT title FROM works WHERE id = ?;"));
        q.addBindValue(wId);
        if (q.exec() && q.next()) {
            return q.value(0).toString();
        }
        return { };
    };

    const qint64 t1 = trackIds.at(0); // A: Snow halation
    const qint64 t2 = trackIds.at(1); // A: Snow halation (Extended Mix)
    const qint64 t3 = trackIds.at(2); // A: Snow halation (Instrumental)
    const qint64 t4 = trackIds.at(3); // A: Song (Elite)
    const qint64 t5 = trackIds.at(4); // A: Other (Forget about my love)
    const qint64 t6 = trackIds.at(5); // B: Snow halation

    const qint64 workA = getTrackWorkId(t1);
    QVERIFY(workA > 0);
    QCOMPARE(getTrackWorkId(t2), workA);
    QCOMPARE(getTrackWorkId(t3), workA);
    QCOMPARE(getWorkTitle(workA), QStringLiteral("Snow halation"));

    // Check version types for A's three tracks
    const auto [base1, vt1, unres1] = getTrackVersion(t1);
    QCOMPARE(base1, QStringLiteral("Snow halation"));
    QCOMPARE(vt1, QStringLiteral("studio"));
    QCOMPARE(unres1, 0);

    const auto [base2, vt2, unres2] = getTrackVersion(t2);
    QCOMPARE(base2, QStringLiteral("Snow halation"));
    QCOMPARE(vt2, QStringLiteral("remix"));
    QCOMPARE(unres2, 0);

    const auto [base3, vt3, unres3] = getTrackVersion(t3);
    QCOMPARE(base3, QStringLiteral("Snow halation"));
    QCOMPARE(vt3, QStringLiteral("instrumental"));
    QCOMPARE(unres3, 0);

    // B's Snow halation is a separate work
    const qint64 workB = getTrackWorkId(t6);
    QVERIFY(workB > 0);
    QVERIFY(workB != workA);
    QCOMPARE(getWorkTitle(workB), QStringLiteral("Snow halation"));
    const auto [base6, vt6, unres6] = getTrackVersion(t6);
    QCOMPARE(base6, QStringLiteral("Snow halation"));
    QCOMPARE(vt6, QStringLiteral("studio"));
    QCOMPARE(unres6, 0);

    // Song (Elite)
    const auto [base4, vt4, unres4] = getTrackVersion(t4);
    QCOMPARE(base4, QStringLiteral("Song (Elite)"));
    QCOMPARE(vt4, QStringLiteral("studio"));
    QCOMPARE(unres4, 0);

    // Other (Forget about my love)
    const auto [base5, vt5, unres5] = getTrackVersion(t5);
    QCOMPARE(unres5, 1);

    // countPending() should be 1 (only the unresolved track)
    const auto pendingRes1 = versionLinker.countPending();
    QVERIFY(pendingRes1.ok());
    QCOMPARE(pendingRes1.value(), 1);

    // 3. Re-run linkAll(1, 0): work IDs and works count remain unchanged
    const auto linkRes2 = versionLinker.linkAll(1, 0);
    QVERIFY(linkRes2.ok());
    const auto &stats2 = linkRes2.value();
    QCOMPARE(stats2.tracks, 6);
    QCOMPARE(stats2.works, 4);
    QCOMPARE(stats2.unresolved, 1);

    QCOMPARE(getTrackWorkId(t1), workA);
    QCOMPARE(getTrackWorkId(t2), workA);
    QCOMPARE(getTrackWorkId(t3), workA);
    QCOMPARE(getTrackWorkId(t6), workB);

    // 4. Delete track B and re-run linkAll: work B is removed
    QSqlQuery delQ(conn);
    QVERIFY(delQ.exec(QStringLiteral("DELETE FROM tracks WHERE id = %1;").arg(t6)));

    const auto linkRes3 = versionLinker.linkAll(1, 0);
    QVERIFY(linkRes3.ok());
    const auto &stats3 = linkRes3.value();
    QCOMPARE(stats3.tracks, 5);
    QCOMPARE(stats3.works, 3);
    QCOMPARE(stats3.unresolved, 1);

    QCOMPARE(getWorkTitle(workB), QString());
}

void TstVersionLinker::linksCrossScriptTitlesWhenMatched()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_cross_script.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/1.m4a"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("17-sai"));
    TestDbHelper::insertRawTag(conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Artist X"));
    TestDbHelper::updateTagsReadAt(conn, t1);

    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/2.flac"));
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("17才"));
    TestDbHelper::insertRawTag(conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Artist X"));
    TestDbHelper::updateTagsReadAt(conn, t2);

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    const ManualClock clock(1000);
    const VersionLinker versionLinker(db, clock);

    // 1. Initially without title matches: linkAll produces 2 works
    const auto linkRes1 = versionLinker.linkAll(1, 1);
    QVERIFY(linkRes1.ok());
    QCOMPARE(linkRes1.value().tracks, 2);
    QCOMPARE(linkRes1.value().works, 2);

    auto getTrackWorkId = [&](qint64 tId) -> qint64 {
        QSqlQuery q(conn);
        q.prepare(QStringLiteral("SELECT work_id FROM tracks WHERE id = ?;"));
        q.addBindValue(tId);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    };

    const qint64 w1 = getTrackWorkId(t1);
    const qint64 w2 = getTrackWorkId(t2);
    QVERIFY(w1 > 0);
    QVERIFY(w2 > 0);
    QVERIFY(w1 != w2);

    // 2. Save a match with low confidence (0.5 < 0.8) -> should NOT merge
    const TitleMatchStore titleStore(db, clock);
    const QString k1 = exactKey(QStringLiteral("17-sai"));
    const QString k2 = exactKey(QStringLiteral("17才"));
    QString kA = k1;
    QString kB = k2;
    if (kA > kB) {
        std::swap(kA, kB);
    }
    const auto keyPair = qMakePair(kA, kB);

    const TitleMatchVerdict lowConfVerdict {
        .same = true,
        .confidence = 0.5,
        .reason = QStringLiteral("Unsure"),
    };
    QVERIFY(titleStore.save({ { keyPair, lowConfVerdict } }, QStringLiteral("test-model"), 1).ok());

    const auto linkRes2 = versionLinker.linkAll(1, 1);
    QVERIFY(linkRes2.ok());
    QCOMPARE(linkRes2.value().works, 2);
    QVERIFY(getTrackWorkId(t1) != getTrackWorkId(t2));

    // 3. Save a match with high confidence (0.95 >= 0.8, same=1) -> should merge into 1 work
    const TitleMatchVerdict highConfVerdict {
        .same = true,
        .confidence = 0.95,
        .reason = QStringLiteral("Romanization match"),
    };
    QVERIFY(
        titleStore.save({ { keyPair, highConfVerdict } }, QStringLiteral("test-model"), 1).ok());

    const auto linkRes3 = versionLinker.linkAll(1, 1);
    QVERIFY(linkRes3.ok());
    QCOMPARE(linkRes3.value().works, 1);
    QCOMPARE(getTrackWorkId(t1), getTrackWorkId(t2));
}

} // namespace

QTEST_GUILESS_MAIN(TstVersionLinker)

#include "tst_VersionLinker.moc"
