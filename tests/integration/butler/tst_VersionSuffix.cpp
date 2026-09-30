// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/TitleVersion.h>
#include <butler/VersionSuffixSource.h>
#include <butler/VersionSuffixStore.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>

namespace {

using linernotes::butler::parseSuffixItemKey;
using linernotes::butler::SuffixClass;
using linernotes::butler::suffixKey;
using linernotes::butler::SuffixRole;
using linernotes::butler::SuffixVerdict;
using linernotes::butler::VersionSuffixSource;
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

class TstVersionSuffix : public QObject {
    Q_OBJECT

private slots:
    void findPendingAndSaveAndLoadAll();
};

void TstVersionSuffix::findPendingAndSaveAndLoadAll()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test_version_suffix.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    const qint64 rootId = TestDbHelper::insertRoot(conn);

    const QStringList titles = {
        QStringLiteral("Snow halation (Extended Mix)"),
        QStringLiteral("Song A (Elite)"),
        QStringLiteral("Song B (Elite) (Instrumental)"),
        QStringLiteral("Song C (Forget about my love)"),
        QStringLiteral("Song D"),
    };

    EntityLinker linker(conn);
    for (qsizetype i = 0; i < titles.size(); ++i) {
        const QString filePath = QStringLiteral("/music/%1.mp3").arg(i + 1);
        const qint64 fileId = TestDbHelper::insertFile(conn, rootId, filePath);
        const qint64 trackId = TestDbHelper::insertTrack(conn, fileId);
        TestDbHelper::insertRawTag(conn, trackId, QStringLiteral("TITLE"), titles.at(i));
        TestDbHelper::updateTagsReadAt(conn, trackId);
        QVERIFY(linker.linkTrack(trackId).ok());
    }

    const VersionSuffixSource source(db);

    // 1. countPending should be 2 (Elite and Forget about my love)
    const auto countRes = source.countPending(1);
    QVERIFY(countRes.ok());
    QCOMPARE(countRes.value(), 2);

    // 2. findItems should return 1 itemKey with exactly 2 samples
    const auto itemsRes = source.findItems(1);
    QVERIFY(itemsRes.ok());
    const auto &items = itemsRes.value();
    QCOMPARE(items.size(), 1);

    const auto samplesRes = parseSuffixItemKey(items.first());
    QVERIFY(samplesRes.ok());
    const auto &samples = samplesRes.value();
    QCOMPARE(samples.size(), 2);

    const QString keyElite = suffixKey(QStringLiteral("Elite"));
    const QString keyForget = suffixKey(QStringLiteral("Forget about my love"));

    QCOMPARE(samples.at(0).key, keyElite);
    QCOMPARE(samples.at(1).key, keyForget);

    // 3. Save verdict for Elite
    const ManualClock clock(1000);
    const VersionSuffixStore store(db, clock);

    const SuffixVerdict verdictElite {
        .cls = SuffixClass {
            .role = SuffixRole::TitlePart,
            .type = VersionType::Studio,
        },
        .confidence = 0.90,
        .reason = QStringLiteral("Song name part"),
    };

    const auto saveRes
        = store.save({ { keyElite, verdictElite } }, QStringLiteral("test-model"), 1);
    QVERIFY(saveRes.ok());

    // 4. After saving, countPending should be 1, findItems should have only 1 sample
    QCOMPARE(source.countPending(1).value(), 1);

    const auto updatedItems = source.findItems(1).value();
    QCOMPARE(updatedItems.size(), 1);
    const auto updatedSamples = parseSuffixItemKey(updatedItems.first()).value();
    QCOMPARE(updatedSamples.size(), 1);
    QCOMPARE(updatedSamples.first().key, keyForget);

    // 5. loadAll with promptVersion 1 should return 1 verdict
    const auto loaded1Res = store.loadAll(1);
    QVERIFY(loaded1Res.ok());
    const auto &loaded1 = loaded1Res.value();
    QCOMPARE(loaded1.size(), 1);
    QVERIFY(loaded1.contains(keyElite));
    QCOMPARE(loaded1.value(keyElite).cls.role, SuffixRole::TitlePart);
    QCOMPARE(loaded1.value(keyElite).confidence, 0.90);

    // 6. loadAll with promptVersion 2 should return 0 verdicts
    const auto loaded2Res = store.loadAll(2);
    QVERIFY(loaded2Res.ok());
    QCOMPARE(loaded2Res.value().size(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstVersionSuffix)

#include "tst_VersionSuffix.moc"
