// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QObject>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <common/ManualClock.h>
#include <library/ArtistAliasCorrections.h>
#include <library/CorrectionStore.h>
#include <library/Database.h>
#include <library/EntityLinker.h>
#include <library/LibraryEnums.h>
#include <library/Migrator.h>
#include <ui/CorrectionBatchModel.h>
#include <ui/CorrectionListModel.h>
#include <ui/CorrectionReviewController.h>
#include <ui/RowSelection.h>

#include <memory>

namespace {

using linernotes::library::ArtistAliasProposal;
using linernotes::library::CorrectionKind;
using linernotes::library::CorrectionProposal;
using linernotes::library::CorrectionSource;
using linernotes::library::CorrectionStatus;
using linernotes::library::CorrectionStore;
using linernotes::library::Database;
using linernotes::library::EntityLinker;
using linernotes::library::Migrator;
using linernotes::library::TagField;
using linernotes::test::ManualClock;
using linernotes::ui::CorrectionBatchModel;
using linernotes::ui::CorrectionListModel;
using linernotes::ui::CorrectionReviewController;

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

    static qint64 getArtistId(const QSqlDatabase &db, const QString &name)
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?;"));
        q.addBindValue(name);
        if (q.exec() && q.next()) {
            return q.value(0).toLongLong();
        }
        return -1;
    }
};

class TstCorrectionReview : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void batchModelListsBatches();
    void listModelShowsBothEntityKinds();
    void acceptSelectedUpdatesStatusAndEmits();
    void revertBatchMarksReverted();
    void staleCorrectionsHandling();
    void deleteBatchRemovesFromListAndSelectsNext();
    void deleteDecidedBatchesClearsProcessed();

private:
    std::unique_ptr<QTemporaryDir> m_tempDir;
    std::unique_ptr<Database> m_db;
    std::unique_ptr<ManualClock> m_clock;
    std::unique_ptr<CorrectionReviewController> m_controller;
    qint64 m_mojibakeBatchId = 0;
    qint64 m_artistMergeBatchId = 0;
};

void TstCorrectionReview::initTestCase()
{
    m_tempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tempDir->isValid());

    m_db = std::make_unique<Database>(
        m_tempDir->filePath(QStringLiteral("test_correction_review.db")));
    QVERIFY(m_db->open(Migrator()).ok());

    const auto conn = m_db->connection().value();
    const qint64 rootId = TestDbHelper::insertRoot(conn);

    const qint64 f1 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song1.mp3"));
    const qint64 f2 = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/song2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    QVERIFY(TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1 Bad")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, t1, QStringLiteral("ARTIST"), QStringLiteral("Canonical Artist")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1));

    QVERIFY(TestDbHelper::insertRawTag(
        conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2 Bad")));
    QVERIFY(TestDbHelper::insertRawTag(
        conn, t2, QStringLiteral("ARTIST"), QStringLiteral("Canonical Artist")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    const qint64 canonicalArtistId
        = TestDbHelper::getArtistId(conn, QStringLiteral("Canonical Artist"));
    QVERIFY(canonicalArtistId > 0);

    m_clock = std::make_unique<ManualClock>(1000);
    CorrectionStore store(*m_db, *m_clock);

    // 1. Create Mojibake batch with 2 proposals
    auto mojibakeRes
        = store.createBatch(CorrectionKind::Mojibake, QStringLiteral("Mojibake Fixes"));
    QVERIFY(mojibakeRes.ok());
    m_mojibakeBatchId = mojibakeRes.value();

    CorrectionProposal p1;
    p1.trackId = t1;
    p1.field = TagField::Title;
    p1.oldValue = std::nullopt;
    p1.newValue = QStringLiteral("Song 1 Fixed");
    p1.source = CorrectionSource::Rule;
    p1.confidence = 0.95;
    p1.reason = QStringLiteral("Guessed from file name");

    CorrectionProposal p2;
    p2.trackId = t2;
    p2.field = TagField::Title;
    p2.oldValue = std::nullopt;
    p2.newValue = QStringLiteral("Song 2 Fixed");
    p2.source = CorrectionSource::Rule;
    p2.confidence = 0.90;
    p2.reason = QStringLiteral("Guessed from file name");

    QVERIFY(store.addProposals(m_mojibakeBatchId, { p1, p2 }).ok());

    // Advance clock so createdAt of artist batch is distinct
    m_clock->advance(1000);

    // 2. Create ArtistMerge batch with 1 alias proposal
    auto artistRes = store.createBatch(CorrectionKind::ArtistMerge, QStringLiteral("Artist Merge"));
    QVERIFY(artistRes.ok());
    m_artistMergeBatchId = artistRes.value();

    ArtistAliasProposal ap;
    ap.canonicalArtistId = canonicalArtistId;
    ap.alias = QStringLiteral("Artist Alias");
    ap.locale = QStringLiteral("en");
    ap.source = CorrectionSource::Rule;
    ap.confidence = 0.85;
    ap.reason = QStringLiteral("Rule-based alias match");

    QVERIFY(store.addArtistAliasProposals(m_artistMergeBatchId, { ap }).ok());

    m_controller = std::make_unique<CorrectionReviewController>(*m_db, *m_clock);
}

void TstCorrectionReview::batchModelListsBatches()
{
    m_controller->refresh();
    const auto *batchModel = m_controller->batchModel();
    QCOMPARE(batchModel->rowCount(), 2);

    const auto b0Kind = batchModel->data(batchModel->index(0, 0), CorrectionBatchModel::KindRole)
                            .value<CorrectionKind>();
    const int b0Pending
        = batchModel->data(batchModel->index(0, 0), CorrectionBatchModel::PendingCountRole).toInt();
    QCOMPARE(b0Kind, CorrectionKind::ArtistMerge);
    QCOMPARE(b0Pending, 1);

    const auto b1Kind = batchModel->data(batchModel->index(1, 0), CorrectionBatchModel::KindRole)
                            .value<CorrectionKind>();
    const int b1Pending
        = batchModel->data(batchModel->index(1, 0), CorrectionBatchModel::PendingCountRole).toInt();
    QCOMPARE(b1Kind, CorrectionKind::Mojibake);
    QCOMPARE(b1Pending, 2);
}

void TstCorrectionReview::listModelShowsBothEntityKinds()
{
    // Select Mojibake batch
    m_controller->selectBatch(m_mojibakeBatchId);
    auto *listModel = m_controller->listModel();
    QCOMPARE(listModel->rowCount(), 2);

    for (int i = 0; i < listModel->rowCount(); ++i) {
        const auto entity = listModel->data(listModel->index(i, 0), CorrectionListModel::EntityRole)
                                .value<CorrectionListModel::CorrectionEntity>();
        QCOMPARE(entity, CorrectionListModel::CorrectionEntity::Track);
        const QString reason
            = listModel->data(listModel->index(i, 0), CorrectionListModel::ReasonRole).toString();
        QCOMPARE(reason, QStringLiteral("Guessed from file name"));
    }

    // Select ArtistMerge batch
    m_controller->selectBatch(m_artistMergeBatchId);
    QCOMPARE(listModel->rowCount(), 1);

    const auto entity = listModel->data(listModel->index(0, 0), CorrectionListModel::EntityRole)
                            .value<CorrectionListModel::CorrectionEntity>();
    QCOMPARE(entity, CorrectionListModel::CorrectionEntity::Artist);

    const QString subject
        = listModel->data(listModel->index(0, 0), CorrectionListModel::SubjectRole).toString();
    QCOMPARE(subject, QStringLiteral("Canonical Artist"));
}

void TstCorrectionReview::acceptSelectedUpdatesStatusAndEmits()
{
    // Select Mojibake batch
    m_controller->selectBatch(m_mojibakeBatchId);
    auto *listModel = m_controller->listModel();
    QCOMPARE(listModel->rowCount(), 2);
    QCOMPARE(listModel->pendingCount(), 2);

    // Select the first row (index 0)
    listModel->selection()->select(0);
    QCOMPARE(listModel->selection()->count(), 1);

    QSignalSpy librarySpy(m_controller.get(), &CorrectionReviewController::libraryModified);
    QSignalSpy countSpy(listModel, &CorrectionListModel::countChanged);

    m_controller->acceptSelected();

    QCOMPARE(librarySpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(listModel->pendingCount(), 1);

    // Row 0 should now be Accepted
    const auto status0 = listModel->data(listModel->index(0, 0), CorrectionListModel::StatusRole)
                             .value<CorrectionStatus>();
    QCOMPARE(status0, CorrectionStatus::Accepted);

    // Row 1 should still be Pending
    const auto status1 = listModel->data(listModel->index(1, 0), CorrectionListModel::StatusRole)
                             .value<CorrectionStatus>();
    QCOMPARE(status1, CorrectionStatus::Pending);
}

void TstCorrectionReview::revertBatchMarksReverted()
{
    QSignalSpy batchCountSpy(m_controller->batchModel(), &CorrectionBatchModel::countChanged);
    m_controller->revertBatch(m_mojibakeBatchId);
    QCOMPARE(batchCountSpy.count(), 1);

    // Check batch in batchModel
    const auto *batchModel = m_controller->batchModel();
    bool found = false;
    for (int i = 0; i < batchModel->rowCount(); ++i) {
        if (batchModel->batchIdAt(i) == m_mojibakeBatchId) {
            const bool reverted
                = batchModel->data(batchModel->index(i, 0), CorrectionBatchModel::RevertedRole)
                      .toBool();
            QCOMPARE(reverted, true);
            found = true;
            break;
        }
    }
    QVERIFY(found);

    // Select Mojibake batch and check item statuses
    m_controller->selectBatch(m_mojibakeBatchId);
    const auto *listModel = m_controller->listModel();
    QCOMPARE(listModel->rowCount(), 2);

    const auto status0 = listModel->data(listModel->index(0, 0), CorrectionListModel::StatusRole)
                             .value<CorrectionStatus>();
    QCOMPARE(status0, CorrectionStatus::Reverted);

    const auto status1 = listModel->data(listModel->index(1, 0), CorrectionListModel::StatusRole)
                             .value<CorrectionStatus>();
    QCOMPARE(status1, CorrectionStatus::Rejected);
}

void TstCorrectionReview::staleCorrectionsHandling()
{
    CorrectionStore store(*m_db, *m_clock);
    const auto batchIdRes
        = store.createBatch(CorrectionKind::Mojibake, QStringLiteral("Stale test batch"));
    QVERIFY(batchIdRes.ok());
    const qint64 batchId = batchIdRes.value();

    const auto conn = m_db->connection().value();
    const qint64 rootId = TestDbHelper::insertRoot(conn, QStringLiteral("/music/stale_test"));
    const qint64 f1
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/stale_test/1.mp3"));
    const qint64 f2
        = TestDbHelper::insertFile(conn, rootId, QStringLiteral("/music/stale_test/2.mp3"));
    const qint64 t1 = TestDbHelper::insertTrack(conn, f1);
    const qint64 t2 = TestDbHelper::insertTrack(conn, f2);

    QVERIFY(
        TestDbHelper::insertRawTag(conn, t1, QStringLiteral("TITLE"), QStringLiteral("Song 1")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t1));
    QVERIFY(
        TestDbHelper::insertRawTag(conn, t2, QStringLiteral("TITLE"), QStringLiteral("Song 2")));
    QVERIFY(TestDbHelper::updateTagsReadAt(conn, t2));

    EntityLinker linker(conn);
    QVERIFY(linker.linkTrack(t1).ok());
    QVERIFY(linker.linkTrack(t2).ok());

    CorrectionProposal p1;
    p1.trackId = t1;
    p1.field = TagField::Title;
    p1.newValue = QStringLiteral("Song 1 Fixed");
    p1.confidence = 0.9;
    p1.reason = QStringLiteral("Fix 1");

    CorrectionProposal p2;
    p2.trackId = t2;
    p2.field = TagField::Title;
    p2.newValue = QStringLiteral("Song 2 Fixed");
    p2.confidence = 0.9;
    p2.reason = QStringLiteral("Fix 2");

    QVERIFY(store.addProposals(batchId, { p1, p2 }).ok());

    m_controller->selectBatch(batchId);
    auto *listModel = m_controller->listModel();
    QCOMPARE(listModel->rowCount(), 2);
    QCOMPARE(listModel->staleCount(), 0);

    // Delete track t2 directly from database (mimicking orphan cleanup)
    QSqlQuery q(conn);
    q.prepare(QStringLiteral("DELETE FROM effective_metadata WHERE track_id = ?;"));
    q.addBindValue(t2);
    QVERIFY(q.exec());

    q.prepare(QStringLiteral("DELETE FROM tracks WHERE id = ?;"));
    q.addBindValue(t2);
    QVERIFY(q.exec());

    // Before listModel is refreshed, allPendingCorrectionIds still has both IDs.
    // Calling acceptAllPending now will process both IDs: t1 accepted, t2 skipped as stale.
    m_controller->acceptAllPending(0.0);

    // After acceptAllPending: noticeText should be non-empty (skippedStale == 1)
    QVERIFY(!m_controller->noticeText().isEmpty());

    // listModel was refreshed by acceptAllPending.
    // staleCount == 1
    QCOMPARE(listModel->staleCount(), 1);

    // Stale filter -> count == 1
    m_controller->showStale();
    QCOMPARE(listModel->statusFilter(), CorrectionListModel::StatusFilter::Stale);
    QCOMPARE(listModel->count(), 1);

    // Pending filter -> does not contain stale item (count == 0 because t1 is accepted, t2 is
    // stale)
    listModel->setStatusFilter(CorrectionListModel::StatusFilter::Pending);
    QCOMPARE(listModel->count(), 0);

    // allPendingCorrectionIds does not contain stale item
    const auto pendingIds = listModel->allPendingCorrectionIds(0.0);
    QCOMPARE(pendingIds.size(), 0);

    // Stale items can be selected and rejected in bulk
    m_controller->showStale();
    listModel->selectAll();
    m_controller->rejectSelected();
    QCOMPARE(listModel->staleCount(), 0);
}

void TstCorrectionReview::deleteBatchRemovesFromListAndSelectsNext()
{
    CorrectionStore store(*m_db, *m_clock);
    const auto extraBatch
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Temporary Batch")).value();
    m_controller->refresh();
    m_controller->selectBatch(extraBatch);
    QCOMPARE(m_controller->currentBatchId(), extraBatch);

    // Delete it
    m_controller->deleteBatch(extraBatch);

    // Current batch switched to first batch in list
    QVERIFY(m_controller->currentBatchId() > 0);
    QVERIFY(m_controller->currentBatchId() != extraBatch);
    QCOMPARE(m_controller->currentBatchId(), m_controller->batchModel()->batchIdAt(0));
}

void TstCorrectionReview::deleteDecidedBatchesClearsProcessed()
{
    CorrectionStore store(*m_db, *m_clock);
    // Create an empty batch (which has 0 pending)
    const auto emptyBatch
        = store.createBatch(CorrectionKind::Manual, QStringLiteral("Empty Decided Batch")).value();
    m_controller->refresh();

    m_controller->deleteDecidedBatches();

    const auto *batchModel = m_controller->batchModel();
    bool foundEmpty = false;
    bool foundPending = false;
    for (int i = 0; i < batchModel->rowCount(); ++i) {
        if (batchModel->batchIdAt(i) == emptyBatch) {
            foundEmpty = true;
        }
        if (batchModel->batchIdAt(i) == m_artistMergeBatchId) {
            foundPending = true;
        }
    }
    QVERIFY(!foundEmpty);
    QVERIFY(foundPending);
}

} // namespace

QTEST_MAIN(TstCorrectionReview)
#include "tst_CorrectionReview.moc"
