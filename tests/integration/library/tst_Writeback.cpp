// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <ai/JobQueue.h>
#include <butler/WritebackJobHandler.h>
#include <common/ManualClock.h>
#include <common/TestSupport.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/LibraryRoots.h>
#include <library/Migrator.h>
#include <library/Scanner.h>
#include <library/TagWriter.h>
#include <library/WritebackStore.h>

namespace {

using linernotes::ai::JobInfo;
using linernotes::ai::JobQueue;
using linernotes::ai::JobState;
using linernotes::butler::WritebackJobHandler;
using linernotes::butler::WritebackRevertJobHandler;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionSource;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::LibraryRoots;
using linernotes::library::Migrator;
using linernotes::library::Scanner;
using linernotes::library::TagField;
using linernotes::library::TagSnapshot;
using linernotes::library::TagWriter;
using linernotes::library::WritebackFileStatus;
using linernotes::library::WritebackStore;
using linernotes::test::fixturePath;
using linernotes::test::ManualClock;

class TstWriteback : public QObject {
    Q_OBJECT

private slots:
    void writebackAndRevert();
    void revertFailsWhenFileModified();
};

void TstWriteback::writebackAndRevert()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test_writeback.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const QString flacSrc = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    const QString mp3Src = fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3"));
    const QString flacDst = tempDir.filePath(QStringLiteral("flac_vorbis.flac"));
    const QString mp3Dst = tempDir.filePath(QStringLiteral("mp3_id3v24_utf8.mp3"));
    QVERIFY(QFile::copy(flacSrc, flacDst));
    QVERIFY(QFile::copy(mp3Src, mp3Dst));

    // Initial snapshot before any write
    const auto initialFlacSnap = TagWriter::snapshot(flacDst);
    QVERIFY(initialFlacSnap.ok());
    const auto initialMp3Snap = TagWriter::snapshot(mp3Dst);
    QVERIFY(initialMp3Snap.ok());

    LibraryRoots roots(db);
    QVERIFY(roots.add(tempDir.path()).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto scanRes = scanner.scanBlocking();
    QVERIFY(scanRes.ok());
    QCOMPARE(scanRes.value().added, 2);

    // Query tracks
    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT t.id, f.id, f.path FROM tracks t "
                                  "JOIN files f ON f.id = t.file_id ORDER BY f.path;")));
    qint64 flacTrackId = 0;
    qint64 flacFileId = 0;
    qint64 mp3TrackId = 0;
    qint64 mp3FileId = 0;
    while (q.next()) {
        const QString path = q.value(2).toString();
        if (path.endsWith(QStringLiteral(".flac"))) {
            flacTrackId = q.value(0).toLongLong();
            flacFileId = q.value(1).toLongLong();
        } else if (path.endsWith(QStringLiteral(".mp3"))) {
            mp3TrackId = q.value(0).toLongLong();
            mp3FileId = q.value(1).toLongLong();
        }
    }
    QVERIFY(flacTrackId > 0 && mp3TrackId > 0);
    q.finish(); // 不留活动语句：否则主线程连接持有旧读快照，工作线程提交后主线程写会
                // SQLITE_BUSY_SNAPSHOT

    ManualClock clock(1000);
    CorrectionStore corrStore(db, clock);
    const auto batchRes = corrStore.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 1"));
    QVERIFY(batchRes.ok());
    const qint64 batchId = batchRes.value();

    const CorrectionProposal p1 {
        .trackId = flacTrackId,
        .field = TagField::Title,
        .oldValue = std::nullopt,
        .newValue = QStringLiteral("New FLAC Title"),
        .source = CorrectionSource::User,
        .confidence = 1.0,
    };
    const CorrectionProposal p2 {
        .trackId = mp3TrackId,
        .field = TagField::Title,
        .oldValue = std::nullopt,
        .newValue = QStringLiteral("New MP3 Title"),
        .source = CorrectionSource::User,
        .confidence = 1.0,
    };
    QVERIFY(corrStore.addProposals(batchId, { p1, p2 }, 0.5).ok());

    WritebackStore wbStore(db);
    const auto planRes = wbStore.plan(batchId);
    QVERIFY(planRes.ok());
    const auto &plan = planRes.value();
    QCOMPARE(plan.files.size(), 2);
    QCOMPARE(plan.skippedCue, 0);
    QCOMPARE(plan.skippedUnsupported, 0);

    const auto createRes = wbStore.create(batchId, plan, clock.nowMs());
    QVERIFY(createRes.ok());
    const qint64 wbId = createRes.value();

    JobQueue queue(db, clock);
    queue.registerHandler(std::make_unique<WritebackJobHandler>(db));
    queue.registerHandler(std::make_unique<WritebackRevertJobHandler>(db));

    const QStringList itemKeys { QString::number(flacFileId), QString::number(mp3FileId) };
    const QJsonObject params { { QStringLiteral("writebackId"), wbId } };
    const auto enqRes = queue.enqueue(
        QStringLiteral("library.writeback"), QStringLiteral("Writeback job"), itemKeys, params);
    QVERIFY(enqRes.ok());
    const qint64 jobId = enqRes.value();

    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state == JobState::Completed, 10000);

    const auto counts = wbStore.counts(wbId);
    QVERIFY(counts.ok());
    QCOMPARE(counts.value().written, 2);
    QCOMPARE(counts.value().failed, 0);

    // Verify snapshot reading back TITLE as new value
    const auto snapFlacAfter = TagWriter::snapshot(flacDst);
    QVERIFY(snapFlacAfter.ok());
    bool flacTitleFound = false;
    for (const auto &b : snapFlacAfter.value().blocks) {
        if (b.properties.value(QStringLiteral("TITLE"))
            == QStringList { QStringLiteral("New FLAC Title") }) {
            flacTitleFound = true;
        }
    }
    QVERIFY(flacTitleFound);

    const auto snapMp3After = TagWriter::snapshot(mp3Dst);
    QVERIFY(snapMp3After.ok());
    bool mp3TitleFound = false;
    for (const auto &b : snapMp3After.value().blocks) {
        if (b.properties.value(QStringLiteral("TITLE"))
            == QStringList { QStringLiteral("New MP3 Title") }) {
            mp3TitleFound = true;
        }
    }
    QVERIFY(mp3TitleFound);

    // 任务项被重试（再跑一次写回任务）不能覆盖已存的写前快照
    const auto retryRes = queue.enqueue(
        QStringLiteral("library.writeback"), QStringLiteral("Writeback retry"), itemKeys, params);
    QVERIFY(retryRes.ok());
    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(retryRes.value()).value_or(JobInfo { }).state == JobState::Completed, 10000);
    const auto flacRecord = wbStore.fileRecord(wbId, flacFileId);
    QVERIFY(flacRecord.ok());
    const auto storedFlacSnap = TagSnapshot::fromJson(
        QJsonDocument::fromJson(flacRecord.value().snapshot.toUtf8()).object());
    QVERIFY(storedFlacSnap.has_value());
    QVERIFY(storedFlacSnap.value_or(TagSnapshot { }) == initialFlacSnap.value());

    // Run revert
    const auto writtenRes = wbStore.writtenFileIds(wbId);
    QVERIFY(writtenRes.ok());
    QCOMPARE(writtenRes.value().size(), 2);

    QStringList revKeys;
    for (const qint64 id : writtenRes.value()) {
        revKeys.append(QString::number(id));
    }
    const auto revEnqRes = queue.enqueue(
        QStringLiteral("library.writeback_revert"), QStringLiteral("Revert job"), revKeys, params);
    QVERIFY(revEnqRes.ok());
    const qint64 revJobId = revEnqRes.value();

    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(revJobId).value_or(JobInfo { }).state == JobState::Completed, 10000);

    QVERIFY(wbStore.finishRevert(wbId, clock.nowMs()).ok());

    const auto revCounts = wbStore.counts(wbId);
    QVERIFY(revCounts.ok());
    QCOMPARE(revCounts.value().reverted, 2);
    QCOMPARE(revCounts.value().revertFailed, 0);
    QCOMPARE(wbStore.activeWriteback(batchId).has_value(), false);

    // Verify snapshots match pre-write snapshot
    const auto snapFlacFinal = TagWriter::snapshot(flacDst);
    QVERIFY(snapFlacFinal.ok());
    QCOMPARE(snapFlacFinal.value(), initialFlacSnap.value());

    const auto snapMp3Final = TagWriter::snapshot(mp3Dst);
    QVERIFY(snapMp3Final.ok());
    QCOMPARE(snapMp3Final.value(), initialMp3Snap.value());
}

void TstWriteback::revertFailsWhenFileModified()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test_writeback_mod.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    const auto &conn = connRes.value();

    const QString flacSrc = fixturePath(QStringLiteral("library/flac_vorbis.flac"));
    const QString mp3Src = fixturePath(QStringLiteral("library/mp3_id3v24_utf8.mp3"));
    const QString flacDst = tempDir.filePath(QStringLiteral("flac_vorbis.flac"));
    const QString mp3Dst = tempDir.filePath(QStringLiteral("mp3_id3v24_utf8.mp3"));
    QVERIFY(QFile::copy(flacSrc, flacDst));
    QVERIFY(QFile::copy(mp3Src, mp3Dst));

    LibraryRoots roots(db);
    QVERIFY(roots.add(tempDir.path()).ok());

    Scanner scanner(db, Scanner::Options { });
    const auto scanRes = scanner.scanBlocking();
    QVERIFY(scanRes.ok());

    QSqlQuery q(conn);
    QVERIFY(q.exec(QStringLiteral("SELECT t.id, f.id, f.path FROM tracks t "
                                  "JOIN files f ON f.id = t.file_id ORDER BY f.path;")));
    qint64 flacTrackId = 0;
    qint64 flacFileId = 0;
    qint64 mp3TrackId = 0;
    qint64 mp3FileId = 0;
    while (q.next()) {
        const QString path = q.value(2).toString();
        if (path.endsWith(QStringLiteral(".flac"))) {
            flacTrackId = q.value(0).toLongLong();
            flacFileId = q.value(1).toLongLong();
        } else if (path.endsWith(QStringLiteral(".mp3"))) {
            mp3TrackId = q.value(0).toLongLong();
            mp3FileId = q.value(1).toLongLong();
        }
    }
    q.finish();

    ManualClock clock(1000);
    CorrectionStore corrStore(db, clock);
    const auto batchRes = corrStore.createBatch(CorrectionKind::Manual, QStringLiteral("Batch 2"));
    QVERIFY(batchRes.ok());
    const qint64 batchId = batchRes.value();

    const CorrectionProposal p1 {
        .trackId = flacTrackId,
        .field = TagField::Title,
        .oldValue = std::nullopt,
        .newValue = QStringLiteral("Modified FLAC Title"),
        .source = CorrectionSource::User,
        .confidence = 1.0,
    };
    const CorrectionProposal p2 {
        .trackId = mp3TrackId,
        .field = TagField::Title,
        .oldValue = std::nullopt,
        .newValue = QStringLiteral("Modified MP3 Title"),
        .source = CorrectionSource::User,
        .confidence = 1.0,
    };
    QVERIFY(corrStore.addProposals(batchId, { p1, p2 }, 0.5).ok());

    WritebackStore wbStore(db);
    const auto planRes = wbStore.plan(batchId);
    QVERIFY(planRes.ok());

    const auto createRes = wbStore.create(batchId, planRes.value(), clock.nowMs());
    QVERIFY(createRes.ok());
    const qint64 wbId = createRes.value();

    JobQueue queue(db, clock);
    queue.registerHandler(std::make_unique<WritebackJobHandler>(db));
    queue.registerHandler(std::make_unique<WritebackRevertJobHandler>(db));

    const QStringList itemKeys { QString::number(flacFileId), QString::number(mp3FileId) };
    const QJsonObject params { { QStringLiteral("writebackId"), wbId } };
    const auto enqRes = queue.enqueue(
        QStringLiteral("library.writeback"), QStringLiteral("Writeback job"), itemKeys, params);
    QVERIFY(enqRes.ok());

    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(enqRes.value()).value_or(JobInfo { }).state == JobState::Completed, 10000);

    // Manually modify flacDst (TagWriter::writeFields change album)
    QHash<TagField, QString> modFields;
    modFields.insert(TagField::Album, QStringLiteral("Externally Modified Album"));
    QVERIFY(TagWriter::writeFields(flacDst, modFields).ok());

    // Run revert
    const auto writtenRes = wbStore.writtenFileIds(wbId);
    QVERIFY(writtenRes.ok());
    QStringList revKeys;
    for (const qint64 id : writtenRes.value()) {
        revKeys.append(QString::number(id));
    }
    const auto revEnqRes = queue.enqueue(
        QStringLiteral("library.writeback_revert"), QStringLiteral("Revert job"), revKeys, params);
    QVERIFY(revEnqRes.ok());

    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(revEnqRes.value()).value_or(JobInfo { }).state == JobState::Completed, 10000);

    const auto revCounts = wbStore.counts(wbId);
    QVERIFY(revCounts.ok());
    QCOMPARE(revCounts.value().reverted, 1);
    QCOMPARE(revCounts.value().revertFailed, 1);

    const auto flacRecord = wbStore.fileRecord(wbId, flacFileId);
    QVERIFY(flacRecord.ok());
    QCOMPARE(flacRecord.value().status, WritebackFileStatus::RevertFailed);

    const auto mp3Record = wbStore.fileRecord(wbId, mp3FileId);
    QVERIFY(mp3Record.ok());
    QCOMPARE(mp3Record.value().status, WritebackFileStatus::Reverted);
}

} // namespace

QTEST_GUILESS_MAIN(TstWriteback)

#include "tst_Writeback.moc"
