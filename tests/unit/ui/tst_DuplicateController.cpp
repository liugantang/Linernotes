// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <butler/DuplicateFinder.h>
#include <butler/DuplicateResolver.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/Database.h>
#include <library/Migrator.h>
#include <ui/DuplicateController.h>

namespace {

using linernotes::butler::FileTrash;
using linernotes::core::Result;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::fixturePath;
using linernotes::test::ManualClock;
using linernotes::ui::DuplicateController;
using linernotes::ui::DuplicateSectionModel;

class TestFileTrash final : public FileTrash {
public:
    explicit TestFileTrash(QString trashDir)
        : m_trashDir(std::move(trashDir))
    {
    }
    ~TestFileTrash() override = default;
    Q_DISABLE_COPY_MOVE(TestFileTrash)

    Result<void> moveToTrash(const QString &path) override
    {
        const QFileInfo fi(path);
        const QString targetPath = QDir(m_trashDir).filePath(fi.fileName());
        if (QFile::exists(targetPath)) {
            QFile::remove(targetPath);
        }
        if (QFile::rename(path, targetPath)) {
            return { };
        }
        if (QFile::copy(path, targetPath) && QFile::remove(path)) {
            return { };
        }
        return linernotes::core::Error {
            .code = QStringLiteral("test.trash_failed"),
            .message = QStringLiteral("Failed to move file to test trash"),
            .detail = path,
        };
    }

private:
    QString m_trashDir;
};

struct TestDbHelper {
    static qint64 insertRoot(const QSqlDatabase &db, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, ?);"));
        q.addBindValue(path);
        q.addBindValue(1000);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertAlbum(const QSqlDatabase &db, qint64 id, const QString &title)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO albums (id, grouping_key, title, created_at) VALUES (?, ?, ?, ?);"));
        q.addBindValue(id);
        q.addBindValue(QStringLiteral("key_%1").arg(id));
        q.addBindValue(title);
        q.addBindValue(1000);
        return q.exec() ? id : -1;
    }

    static qint64 insertFile(const QSqlDatabase &db, qint64 rootId, const QString &path)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO files (root_id, path, size, mtime, content_hash, codec, sample_rate, "
            "bit_depth, bitrate, duration_ms, first_seen_at, scanned_at) "
            "VALUES (?, ?, 1048576, 2000, 'hash', 'flac', 44100, 16, 0, 8000, 2000, 2000);"));
        q.addBindValue(rootId);
        q.addBindValue(path);
        return q.exec() ? q.lastInsertId().toLongLong() : -1;
    }

    static qint64 insertTrack(const QSqlDatabase &db, qint64 trackId, qint64 fileId, qint64 albumId)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO tracks (id, file_id, cue_index, album_id, tags_read_at, created_at) "
            "VALUES (?, ?, NULL, ?, 1000, 3000);"));
        q.addBindValue(trackId);
        q.addBindValue(fileId);
        q.addBindValue(albumId);
        return q.exec() ? trackId : -1;
    }

    static bool insertMetadata(const QSqlDatabase &db, qint64 trackId, const QString &title,
        const QString &artist, const QString &album)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO effective_metadata (track_id, title, artist, album, updated_at) "
            "VALUES (?, ?, ?, ?, 1000);"));
        q.addBindValue(trackId);
        q.addBindValue(title);
        q.addBindValue(artist);
        q.addBindValue(album);
        return q.exec();
    }

    static bool insertDuplicateGroup(const QSqlDatabase &db, qint64 groupId,
        const QString &kind = QStringLiteral("same_recording"), qint64 createdAt = 1000)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO duplicate_groups (id, kind, created_at) VALUES (?, ?, ?);"));
        q.addBindValue(groupId);
        q.addBindValue(kind);
        q.addBindValue(createdAt);
        return q.exec();
    }

    static bool insertDuplicateMember(
        const QSqlDatabase &db, qint64 groupId, qint64 trackId, double keepScore, int recommended)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO duplicate_members (group_id, track_id, keep_score, recommended) "
            "VALUES (?, ?, ?, ?);"));
        q.addBindValue(groupId);
        q.addBindValue(trackId);
        q.addBindValue(keepScore);
        q.addBindValue(recommended);
        return q.exec();
    }
};

class TstDuplicateController : public QObject {
    Q_OBJECT

private slots:
    void refreshLoadsSectionsAndRecommendedAlbum();
    void keepAlbumKeepsAlbumATracksAndRemovesAlbumBTracks();
    void dismissSectionDismissesAllGroups();
};

void TstDuplicateController::refreshLoadsSectionsAndRecommendedAlbum()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QTemporaryDir trashDir;
    QVERIFY(trashDir.isValid());

    const QString fixtureMelody = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString fA1 = musicDir.filePath(QStringLiteral("a1.flac"));
    const QString fA2 = musicDir.filePath(QStringLiteral("a2.flac"));
    const QString fB1 = musicDir.filePath(QStringLiteral("b1.flac"));
    const QString fB2 = musicDir.filePath(QStringLiteral("b2.flac"));

    QVERIFY(QFile::copy(fixtureMelody, fA1));
    QVERIFY(QFile::copy(fixtureMelody, fA2));
    QVERIFY(QFile::copy(fixtureMelody, fB1));
    QVERIFY(QFile::copy(fixtureMelody, fB2));

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn, musicDir.path());
    QVERIFY(rootId > 0);

    const qint64 albA = TestDbHelper::insertAlbum(conn, 1, QStringLiteral("Album A"));
    const qint64 albB = TestDbHelper::insertAlbum(conn, 2, QStringLiteral("Album B"));
    QCOMPARE(albA, 1LL);
    QCOMPARE(albB, 2LL);

    const qint64 fileA1 = TestDbHelper::insertFile(conn, rootId, fA1);
    const qint64 fileA2 = TestDbHelper::insertFile(conn, rootId, fA2);
    const qint64 fileB1 = TestDbHelper::insertFile(conn, rootId, fB1);
    const qint64 fileB2 = TestDbHelper::insertFile(conn, rootId, fB2);

    TestDbHelper::insertTrack(conn, 1, fileA1, albA);
    TestDbHelper::insertTrack(conn, 2, fileA2, albA);
    TestDbHelper::insertTrack(conn, 3, fileB1, albB);
    TestDbHelper::insertTrack(conn, 4, fileB2, albB);

    TestDbHelper::insertMetadata(
        conn, 1, QStringLiteral("Track 1"), QStringLiteral("Artist"), QStringLiteral("Album A"));
    TestDbHelper::insertMetadata(
        conn, 2, QStringLiteral("Track 2"), QStringLiteral("Artist"), QStringLiteral("Album A"));
    TestDbHelper::insertMetadata(
        conn, 3, QStringLiteral("Track 1"), QStringLiteral("Artist"), QStringLiteral("Album B"));
    TestDbHelper::insertMetadata(
        conn, 4, QStringLiteral("Track 2"), QStringLiteral("Artist"), QStringLiteral("Album B"));

    // Two duplicate groups: group 1 has (track 1, track 3), group 2 has (track 2, track 4)
    // Album B members have higher keep_score (600 vs 500)
    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, 101));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 101, 1, 500.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 101, 3, 600.0, 1));

    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, 102));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 102, 2, 500.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 102, 4, 600.0, 1));

    const ManualClock clock(1000);
    TestFileTrash trash(trashDir.path());
    DuplicateController controller(db, clock, trash);

    controller.refresh();
    QTRY_COMPARE(controller.groupCount(), 2);
    QCOMPARE(controller.sectionCount(), 1);

    const auto *model = controller.sectionModel();
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 1);

    const QModelIndex idx = model->index(0, 0);
    QCOMPARE(model->data(idx, DuplicateSectionModel::RecommendedAlbumIdRole).toLongLong(), 2LL);
    QCOMPARE(model->data(idx, DuplicateSectionModel::GroupCountRole).toInt(), 2);
    QCOMPARE(model->data(idx, DuplicateSectionModel::TitleRole).toString(),
        QStringLiteral("Album A ↔ Album B"));
}

void TstDuplicateController::keepAlbumKeepsAlbumATracksAndRemovesAlbumBTracks()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QTemporaryDir trashDir;
    QVERIFY(trashDir.isValid());

    const QString fixtureMelody = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString fA1 = musicDir.filePath(QStringLiteral("a1.flac"));
    const QString fA2 = musicDir.filePath(QStringLiteral("a2.flac"));
    const QString fB1 = musicDir.filePath(QStringLiteral("b1.flac"));
    const QString fB2 = musicDir.filePath(QStringLiteral("b2.flac"));

    QVERIFY(QFile::copy(fixtureMelody, fA1));
    QVERIFY(QFile::copy(fixtureMelody, fA2));
    QVERIFY(QFile::copy(fixtureMelody, fB1));
    QVERIFY(QFile::copy(fixtureMelody, fB2));

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn, musicDir.path());
    QVERIFY(rootId > 0);

    const qint64 albA = TestDbHelper::insertAlbum(conn, 1, QStringLiteral("Album A"));
    const qint64 albB = TestDbHelper::insertAlbum(conn, 2, QStringLiteral("Album B"));

    const qint64 fileA1 = TestDbHelper::insertFile(conn, rootId, fA1);
    const qint64 fileA2 = TestDbHelper::insertFile(conn, rootId, fA2);
    const qint64 fileB1 = TestDbHelper::insertFile(conn, rootId, fB1);
    const qint64 fileB2 = TestDbHelper::insertFile(conn, rootId, fB2);

    TestDbHelper::insertTrack(conn, 1, fileA1, albA);
    TestDbHelper::insertTrack(conn, 2, fileA2, albA);
    TestDbHelper::insertTrack(conn, 3, fileB1, albB);
    TestDbHelper::insertTrack(conn, 4, fileB2, albB);

    TestDbHelper::insertMetadata(
        conn, 1, QStringLiteral("Track 1"), QStringLiteral("Artist"), QStringLiteral("Album A"));
    TestDbHelper::insertMetadata(
        conn, 2, QStringLiteral("Track 2"), QStringLiteral("Artist"), QStringLiteral("Album A"));
    TestDbHelper::insertMetadata(
        conn, 3, QStringLiteral("Track 1"), QStringLiteral("Artist"), QStringLiteral("Album B"));
    TestDbHelper::insertMetadata(
        conn, 4, QStringLiteral("Track 2"), QStringLiteral("Artist"), QStringLiteral("Album B"));

    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, 101));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 101, 1, 500.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 101, 3, 600.0, 1));

    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, 102));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 102, 2, 500.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 102, 4, 600.0, 1));

    const ManualClock clock(1000);
    TestFileTrash trash(trashDir.path());
    DuplicateController controller(db, clock, trash);

    controller.refresh();
    QTRY_COMPARE(controller.groupCount(), 2);

    // Keep Album A (albumId = 1) across the section
    controller.keepAlbum(0, albA);
    QTRY_COMPARE(controller.isBusy(), false);
    QTRY_COMPARE(controller.groupCount(), 0);

    // Verify tracks from Album A still exist, Album B deleted
    QSqlQuery checkQ(conn);
    QVERIFY(checkQ.exec(QStringLiteral("SELECT id FROM tracks ORDER BY id ASC;")));
    QList<qint64> remainingTrackIds;
    while (checkQ.next()) {
        remainingTrackIds.append(checkQ.value(0).toLongLong());
    }
    QCOMPARE(remainingTrackIds, (QList<qint64> { 1, 2 }));

    // Verify files
    QVERIFY(QFile::exists(fA1));
    QVERIFY(QFile::exists(fA2));
    QVERIFY(!QFile::exists(fB1));
    QVERIFY(!QFile::exists(fB2));
    QVERIFY(QFile::exists(QDir(trashDir.path()).filePath(QStringLiteral("b1.flac"))));
    QVERIFY(QFile::exists(QDir(trashDir.path()).filePath(QStringLiteral("b2.flac"))));
}

void TstDuplicateController::dismissSectionDismissesAllGroups()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QTemporaryDir trashDir;
    QVERIFY(trashDir.isValid());

    const QString fixtureMelody = fixturePath(QStringLiteral("audio/melody_8s.flac"));
    const QString fA1 = musicDir.filePath(QStringLiteral("a1.flac"));
    const QString fA2 = musicDir.filePath(QStringLiteral("a2.flac"));
    const QString fB1 = musicDir.filePath(QStringLiteral("b1.flac"));
    const QString fB2 = musicDir.filePath(QStringLiteral("b2.flac"));

    QVERIFY(QFile::copy(fixtureMelody, fA1));
    QVERIFY(QFile::copy(fixtureMelody, fA2));
    QVERIFY(QFile::copy(fixtureMelody, fB1));
    QVERIFY(QFile::copy(fixtureMelody, fB2));

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const qint64 rootId = TestDbHelper::insertRoot(conn, musicDir.path());
    QVERIFY(rootId > 0);

    const qint64 albA = TestDbHelper::insertAlbum(conn, 1, QStringLiteral("Album A"));
    const qint64 albB = TestDbHelper::insertAlbum(conn, 2, QStringLiteral("Album B"));

    const qint64 fileA1 = TestDbHelper::insertFile(conn, rootId, fA1);
    const qint64 fileA2 = TestDbHelper::insertFile(conn, rootId, fA2);
    const qint64 fileB1 = TestDbHelper::insertFile(conn, rootId, fB1);
    const qint64 fileB2 = TestDbHelper::insertFile(conn, rootId, fB2);

    TestDbHelper::insertTrack(conn, 1, fileA1, albA);
    TestDbHelper::insertTrack(conn, 2, fileA2, albA);
    TestDbHelper::insertTrack(conn, 3, fileB1, albB);
    TestDbHelper::insertTrack(conn, 4, fileB2, albB);

    TestDbHelper::insertMetadata(
        conn, 1, QStringLiteral("Track 1"), QStringLiteral("Artist"), QStringLiteral("Album A"));
    TestDbHelper::insertMetadata(
        conn, 2, QStringLiteral("Track 2"), QStringLiteral("Artist"), QStringLiteral("Album A"));
    TestDbHelper::insertMetadata(
        conn, 3, QStringLiteral("Track 1"), QStringLiteral("Artist"), QStringLiteral("Album B"));
    TestDbHelper::insertMetadata(
        conn, 4, QStringLiteral("Track 2"), QStringLiteral("Artist"), QStringLiteral("Album B"));

    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, 101));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 101, 1, 500.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 101, 3, 600.0, 1));

    QVERIFY(TestDbHelper::insertDuplicateGroup(conn, 102));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 102, 2, 500.0, 0));
    QVERIFY(TestDbHelper::insertDuplicateMember(conn, 102, 4, 600.0, 1));

    const ManualClock clock(1000);
    TestFileTrash trash(trashDir.path());
    DuplicateController controller(db, clock, trash);

    controller.refresh();
    QTRY_COMPARE(controller.groupCount(), 2);

    controller.dismissSection(0);
    QTRY_COMPARE(controller.isBusy(), false);
    QTRY_COMPARE(controller.groupCount(), 0);

    // Verify duplicate_groups deleted
    QSqlQuery countQ(conn);
    QVERIFY(countQ.exec(QStringLiteral("SELECT COUNT(*) FROM duplicate_groups;")));
    QVERIFY(countQ.next());
    QCOMPARE(countQ.value(0).toInt(), 0);

    // Verify duplicate_dismissals contains the 2 pairs
    QSqlQuery dismQ(conn);
    QVERIFY(dismQ.exec(QStringLiteral("SELECT COUNT(*) FROM duplicate_dismissals;")));
    QVERIFY(dismQ.next());
    QCOMPARE(dismQ.value(0).toInt(), 2);

    // All tracks remain
    QSqlQuery trkQ(conn);
    QVERIFY(trkQ.exec(QStringLiteral("SELECT COUNT(*) FROM tracks;")));
    QVERIFY(trkQ.next());
    QCOMPARE(trkQ.value(0).toInt(), 4);
}

} // namespace

QTEST_GUILESS_MAIN(TstDuplicateController)
#include "tst_DuplicateController.moc"
